/*
	Copyright (c) 2011-2013 Tim Thompson <me@timthompson.com>

	Permission is hereby granted, free of charge, to any person obtaining
	a copy of this software and associated documentation files
	(the "Software"), to deal in the Software without restriction,
	including without limitation the rights to use, copy, modify, merge,
	publish, distribute, sublicense, and/or sell copies of the Software,
	and to permit persons to whom the Software is furnished to do so,
	subject to the following conditions:

	The above copyright notice and this permission notice shall be
	included in all copies or substantial portions of the Software.

	Any person wishing to distribute modifications to the Software is
	requested to send the modifications to the original developer so that
	they can be incorporated into the canonical version.

	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
	EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
	MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
	IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR
	ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
	CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
	WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <pthread.h>

#include <iostream>
#include <sstream>
#include <fstream>
#include <strstream>
#include <cstdlib> // for srand, rand
#include <ctime>   // for time
#include <sys/stat.h>

#include "UT_SharedMem.h"
#include "NosuchUtil.h"
#include "NosuchMidi.h"
#include "NosuchScheduler.h"
#include "Vizlet.h"
#include "NosuchJSON.h"

#include "ffutil.h"
#include "FFGLLib.h"
#include "osc/OscOutboundPacketStream.h"
#include "osc/OscReceivedElements.h"
#include "NosuchOsc.h"

////////////////////////////////////////////////////////////////////////////////////////////////////
//  Plugin information
////////////////////////////////////////////////////////////////////////////////////////////////////
#ifdef _WIN32

// These functions need to be defined in a vizlet's source file.
extern std::string vizlet_name();
extern void vizlet_setdll(std::string(dllpath));
CFFGLPluginInfo& vizlet_plugininfo();

void vizlet_setid(CFFGLPluginInfo& plugininfo, std::string name)
{
	char id[5];
	// Compute a hash of the plugin name and use two 4-bit values
	// from it to produce the last 2 characters of the unique ID.
	// It's possible there will be a collision.
	int hash = 0;
	for ( const char* p = name.c_str(); *p!='\0'; p++ ) {
		hash += *p;
	}
	id[0] = 'V';
	id[1] = 'Z';
	id[2] = 'A' + (hash & 0xf);
	id[3] = 'A' + ((hash >> 4) & 0xf);
	id[4] = '\0';
	plugininfo.SetPluginIdAndName(id,name.c_str());
}

char dllpath[MAX_PATH];
std::string dllpathstr;

std::string dll_pathname() {
	return(dllpathstr);
}

BOOL APIENTRY DllMain(HANDLE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved)
{
	GetModuleFileNameA((HMODULE)hModule, dllpath, MAX_PATH);
	dllpathstr = std::string(dllpath);

	if (ul_reason_for_call == DLL_PROCESS_ATTACH ) {
		// Initialize once for each new process.
		// Return FALSE if we fail to load DLL.
		dllpathstr = NosuchToLower(dllpathstr);
		if ( ! default_setdll(dllpathstr) ) {
			DEBUGPRINT(("default_setdll failed"));
			return FALSE;
		}
		vizlet_setdll(dllpathstr);
		std::string s = vizlet_name();
		DEBUGPRINT1(("After vizlet_setdll, dllpathstr=%s vizlet_name=%s",dllpathstr.c_str(),s.c_str()));
		vizlet_setid(vizlet_plugininfo(),s);
		DEBUGPRINT1(("DLL_PROCESS_ATTACH dll=%s",dllpath));
	}
	if (ul_reason_for_call == DLL_PROCESS_DETACH ) {
		DEBUGPRINT1(("DLL_PROCESS_DETACH dll=%s",dllpath));
	}
	if (ul_reason_for_call == DLL_THREAD_ATTACH ) {
		DEBUGPRINT1(("DLL_THREAD_ATTACH dll=%s",dllpath));
	}
	if (ul_reason_for_call == DLL_THREAD_DETACH ) {
		DEBUGPRINT1(("DLL_THREAD_DETACH dll=%s",dllpath));
	}
    return TRUE;
}

#endif

Vizlet::Vizlet() {

	DEBUGPRINT1(("=== Vizlet constructor, dll_pathname=%s",dll_pathname().c_str()));

	VizParams::Initialize();

	_defaultparams = new AllVizParams(true);

	AllVizParams* spdefault = getAllVizParams("default");
	if ( spdefault ) {
		_defaultparams->applyVizParamsFrom(spdefault);
	}

	_callbacksInitialized = false;
	_passthru = true;
	_spritelist = new VizSpriteList();
	_defaultmidiparams = defaultParams();
	// _frame = 0;

	_vizserver = VizServer::GetServer();

	// _viztag = "UNTAGGED";
	_viztag = vizlet_name();

	_af = ApiFilter(_viztag.c_str());
	_mf = MidiFilter();  // ALL Midi
	_cf = CursorFilter();  // ALL Cursors

	// These are default values, which can be overridden by the config file.

	// _do_errorpopup = false;

#ifdef PALETTE_PYTHON
	_recompileFunc = NULL;
	_python_enabled = FALSE;
	_python_events_disabled = TRUE;
	NosuchLockInit(&python_mutex,"python");
#endif

	NosuchLockInit(&vizlet_mutex,"vizlet");

	NosuchLockInit(&json_mutex,"json");
	json_cond = PTHREAD_COND_INITIALIZER;
	json_pending = false;

	_disabled = false;
	_disable_on_exception = false;

	_stopped = false;

	// The most common reason for being disabled at this point
	// is when the config JSON file can't be parsed.
	if ( _disabled ) {
		DEBUGPRINT(("WARNING! Vizlet (viztag=%s) has been disabled!",VizTag().c_str()));
	}

	SetMinInputs(1);
	SetMaxInputs(1);

	SetParamInfo(0,"viztag", FF_TYPE_TEXT, VizTag().c_str());
}

Vizlet::~Vizlet()
{
	DEBUGPRINT1(("=== Vizlet destructor is called viztag=%s", _viztag.c_str()));
	_stopstuff();
}

DWORD Vizlet::SetParameter(const SetParameterStruct* pParam) {

	DWORD r = FF_FAIL;
	bool changed = false;

	// Sometimes SetParameter is called before ProcessOpenGL,
	// so make sure the VizServer is started.
	StartVizServer();
	InitCallbacks();

	switch ( pParam->ParameterNumber ) {
	case 0:		// shape
		if ( VizTag() != std::string(pParam->u.NewTextValue) ) {
			changed = true;
			SetVizTag(pParam->u.NewTextValue);
			_af = ApiFilter(VizTag().c_str());
			ChangeVizTag(pParam->u.NewTextValue);
		}
		r = FF_SUCCESS;
	}
	return r;
}

DWORD Vizlet::GetParameter(DWORD n) {
	switch ( n ) {
	case 0:		// shape
		return (DWORD)(VizTag().c_str());
	}
	return FF_FAIL;
}

char* Vizlet::GetParameterDisplay(DWORD n)
{
	switch ( n ) {
	case 0:
	    strncpy_s(_disp,DISPLEN,VizTag().c_str(),VizTag().size());
	    _disp[DISPLEN-1] = 0;
		return _disp;
	}
	return "";
}

const char* vizlet_json(void* data,const char *method, cJSON* params, const char* id) {
	Vizlet* v = (Vizlet*)data;
	if ( v == NULL ) {
		static std::string err = error_json(-32000,"v is NULL is vizlet_json?",id);
		return err.c_str();
	}
	else {
		// A few methods are built-in.  If it isn't one of those,
		// it is given to the plugin to handle.
		if (std::string(method) == "description") {
			std::string desc = vizlet_plugininfo().GetPluginExtendedInfo()->Description;
			v->json_result = jsonStringResult(desc, id);
		} else if (std::string(method) == "about") {
			std::string desc = vizlet_plugininfo().GetPluginExtendedInfo()->About;
			v->json_result = jsonStringResult(desc, id);
		} else {
			v->json_result = v->processJsonAndCatchExceptions(std::string(method), params, id);
		}
		return v->json_result.c_str();
	}
}

void vizlet_osc(void* data,const char *source, const osc::ReceivedMessage& m) {
	Vizlet* v = (Vizlet*)data;
	NosuchAssert(v);
	v->processOsc(source,m);
}

void vizlet_midiinput(void* data,MidiMsg* m) {
	Vizlet* v = (Vizlet*)data;
	NosuchAssert(v);
	v->processMidiInput(m);
}

void vizlet_midioutput(void* data,MidiMsg* m) {
	Vizlet* v = (Vizlet*)data;
	NosuchAssert(v);
	v->processMidiOutput(m);
}

void vizlet_cursor(void* data,VizCursor* c, int downdragup) {
	Vizlet* v = (Vizlet*)data;
	NosuchAssert(v);
	v->processCursor(c,downdragup);
}

void vizlet_keystroke(void* data,int key, int downup) {
	Vizlet* v = (Vizlet*)data;
	NosuchAssert(v);
	v->processKeystroke(key,downup);
}

void Vizlet::advanceCursorTo(VizCursor* c, int tm) {
	_vizserver->AdvanceCursorTo(c,tm);
}

void Vizlet::ChangeVizTag(const char* p) {
	_vizserver->ChangeVizTag(Handle(),p);
}

void Vizlet::_startApiCallbacks(ApiFilter af, void* data) {
	NosuchAssert(_vizserver);
	_vizserver->AddJsonCallback(Handle(),af,vizlet_json,data);
}

void Vizlet::_startMidiCallbacks(MidiFilter mf, void* data) {
	NosuchAssert(_vizserver);
	_vizserver->AddMidiInputCallback(Handle(),mf,vizlet_midiinput,data);
	_vizserver->AddMidiOutputCallback(Handle(),mf,vizlet_midioutput,data);
}

void Vizlet::_startCursorCallbacks(CursorFilter cf, void* data) {
	NosuchAssert(_vizserver);
	_vizserver->AddCursorCallback(Handle(),cf,vizlet_cursor,data);
}

void Vizlet::_startKeystrokeCallbacks(void* data) {
	NosuchAssert(_vizserver);
	_vizserver->AddKeystrokeCallback(Handle(),vizlet_keystroke,data);
}

void Vizlet::_stopCallbacks() {
	_stopApiCallbacks();
	_stopMidiCallbacks();
	_stopCursorCallbacks();
}

void Vizlet::_stopApiCallbacks() {
	NosuchAssert(_vizserver);
	_vizserver->RemoveJsonCallback(Handle());
}

void Vizlet::_stopMidiCallbacks() {
	NosuchAssert(_vizserver);
	_vizserver->RemoveMidiInputCallback(Handle());
	_vizserver->RemoveMidiOutputCallback(Handle());
}

void Vizlet::_stopCursorCallbacks() {
	NosuchAssert(_vizserver);
	_vizserver->RemoveCursorCallback(Handle());
}

int Vizlet::MilliNow() {
	if ( _vizserver == NULL ) {
		DEBUGPRINT(("Vizlet::MilliNow() - _vizserver is NULL!"));
		return 0;
	} else {
		return _vizserver->MilliNow();
	}
}

int Vizlet::CurrentClick() {
	if ( _vizserver == NULL ) {
		DEBUGPRINT(("Vizlet::CurrentClick() - _vizserver is NULL!"));
		return 0;
	} else {
		return _vizserver->CurrentClick();
	}
}

void Vizlet::SendMidiMsg() {
	DEBUGPRINT(("Vizlet::SendMidiMsg called - this should go away eventually"));
	MidiMsg* msg = MidiNoteOn::make(1,80,100);
	// _vizserver->MakeNoteOn(1,80,100);
	_vizserver->SendMidiMsg(msg);
}


void
Vizlet::_drawnotes(std::list<MidiMsg*>& notes) {

	for ( std::list<MidiMsg*>::const_iterator ci = notes.begin(); ci != notes.end(); ) {
		MidiMsg* m = *ci++;
		if (m->MidiType() == MIDI_NOTE_ON) {
			processDrawNote(m);
		}
	}
}

#if 0
		if (m->MidiType() != MIDI_NOTE_ON ) {
			continue;
		}
		int pitch = m->Pitch();
		float y = pitch  / 128.0f;
		y = y * 2.0f - 1.0f;
		float x = _x;
		_x += 0.01f;
		if ( _x >= 1.0 ) {
			_x = -0.9f;
		}
		float sz = 0.15f;

		glBegin(GL_LINE_LOOP);
		glVertex3f(x-sz, y-sz, 0.0f);	// Top Left
		glVertex3f(x-sz, y+sz, 0.0f);	// Top Right
		glVertex3f(x+sz, y+sz, 0.0f);	// Bottom Right
		glVertex3f(x+sz, y-sz, 0.0f);	// Bottom Left
		glEnd();
	}
}
#endif

DWORD Vizlet::InitGL(const FFGLViewportStruct *vp) {
	return FF_SUCCESS;
}

void Vizlet::_stopstuff() {
	if ( _stopped )
		return;
	_stopped = true;
	_stopCallbacks();
	if ( _vizserver ) {
		int ncb = _vizserver->NumCallbacks();
		int mcb = _vizserver->MaxCallbacks();
		DEBUGPRINT1(("Vizlet::_stopstuff - VizServer has %d callbacks, max=%d!",ncb,mcb));
		if ( ncb == 0 && mcb > 0 ) {
			_vizserver->Stop();
		}
	}
}

DWORD Vizlet::DeInitGL() {
	_stopstuff();
	return FF_SUCCESS;
}

void Vizlet::AddVizSprite(VizSprite* s) {
	_spritelist->add(s,defaultParams()->nsprites);
}

void Vizlet::DrawVizSprites() {
	_spritelist->draw(&graphics);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
//  Methods
////////////////////////////////////////////////////////////////////////////////////////////////////

void
Vizlet::QueueMidiPhrase(MidiPhrase* ph, click_t clk) {
	_vizserver->QueueMidiPhrase(ph,clk);
}

void
Vizlet::QueueMidiMsg(MidiMsg* m, click_t clk) {
	_vizserver->QueueMidiMsg(m,clk);
}

void
Vizlet::QueueClear() {
	_vizserver->QueueClear();
}

void
Vizlet::StartVizServer() {
	if ( ! _vizserver->Started() ) {
		_vizserver->Start();
		srand( _vizserver->MilliNow() );
	}
}

void
Vizlet::InitCallbacks() {
	if ( ! _callbacksInitialized ) {

		_startApiCallbacks(_af,(void*)this);
		_startMidiCallbacks(_mf,(void*)this);
		_startCursorCallbacks(_cf,(void*)this);
		_startKeystrokeCallbacks((void*)this);

		_callbacksInitialized = true;
	}
}

DWORD Vizlet::ProcessOpenGL(ProcessOpenGLStruct *pGL)
{
	if ( _stopped ) {
		return FF_SUCCESS;
	}
	if ( _disabled ) {
		return FF_SUCCESS;
	}

	StartVizServer();
	InitCallbacks();

#ifdef FRAMELOOPINGTEST
	static int framenum = 0;
	static bool framelooping = FALSE;
#endif

	NosuchLock(&json_mutex,"json");
	if (json_pending) {
		// Execute json stuff and generate response
	
		json_result = processJsonAndCatchExceptions(json_method, json_params, json_id);
		json_pending = false;
		int e = pthread_cond_signal(&json_cond);
		if ( e ) {
			DEBUGPRINT(("ERROR from pthread_cond_signal e=%d\n",e));
		}
	}
	NosuchUnlock(&json_mutex,"json");

	if ( _passthru && pGL != NULL ) {
		if ( ! ff_passthru(pGL) ) {
			return FF_FAIL;
		}
	}

	LockVizlet();

	// _frame++;

	bool gotexception = false;
	try {
		CATCH_NULL_POINTERS;

		glDisable(GL_TEXTURE_2D); 
		glEnable(GL_BLEND); 
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); 
		glLineWidth((GLfloat)1.0f);

		glScaled(2.0,2.0,1.0);
		glTranslated(-0.5,-0.5,0.0);

		int milli = MilliNow();
		bool r = processDraw();			// Call the vizlet's processDraw()

		glDisable(GL_TEXTURE_2D);
		glColor4f(1.f,1.f,1.f,1.f); //restore default color

		_spritelist->advanceTo(milli);
		processAdvanceTimeTo(milli);

		if ( ! r ) {
			DEBUGPRINT(("Palette::draw returned failure? (r=%d)\n",r));
			gotexception = true;
		}
	} catch (NosuchException& e ) {
		DEBUGPRINT(("NosuchException in Palette::draw : %s",e.message()));
		gotexception = true;
	} catch (...) {
		DEBUGPRINT(("UNKNOWN Exception in Palette::draw!"));
		gotexception = true;
	}

	if ( gotexception && _disable_on_exception ) {
		DEBUGPRINT(("DISABLING Vizlet due to exception!!!!!"));
		_disabled = true;
	}

	UnlockVizlet();

	glDisable(GL_BLEND); 
	glEnable(GL_TEXTURE_2D); 
	// END NEW CODE

#ifdef FRAMELOOPINGTEST
	int w = Texture.Width;
	int h = Texture.Height;
#define NFRAMES 300
	static GLubyte* pixelArray[NFRAMES];
	if ( framelooping ) {
		glRasterPos2i(-1,-1);
		glDrawPixels(w,h,GL_RGB,GL_UNSIGNED_BYTE,pixelArray[framenum]);
		framenum = (framenum+1)%NFRAMES;
	} else {
		if ( framenum < NFRAMES ) {
			pixelArray[framenum] = new GLubyte[w*h*3];
			glReadPixels(0,0,w,h,GL_RGB,GL_UNSIGNED_BYTE,pixelArray[framenum]);
			framenum++;
		} else {
			framelooping = TRUE;
			framenum = 0;
		}
	}
#endif

	//disable texturemapping
	glDisable(GL_TEXTURE_2D);
	
	//restore default color
	glColor4d(1.f,1.f,1.f,1.f);
	
	return FF_SUCCESS;
}

void Vizlet::DrawNotesDown() {
	_vizserver->LockNotesDown();
	try {
		CATCH_NULL_POINTERS;
		_drawnotes(_vizserver->NotesDown());
	} catch (NosuchException& e) {
		DEBUGPRINT(("NosuchException while drawing notes: %s",e.message()));
	} catch (...) {
		DEBUGPRINT(("Some other kind of exception occured while drawing notes!?"));
	}
	_vizserver->UnlockNotesDown();
}

void Vizlet::LockVizlet() {
	NosuchLock(&vizlet_mutex,"Vizlet");
}

void Vizlet::UnlockVizlet() {
	NosuchUnlock(&vizlet_mutex,"Vizlet");
}

std::string Vizlet::processJsonAndCatchExceptions(std::string meth, cJSON *params, const char *id) {
	std::string r;
	try {
		CATCH_NULL_POINTERS;

		// Here, we should intercept and interpret some APIs that are common to all vizlets
		if ( meth == "echo"  ) {
			std::string val = jsonNeedString(params,"value");
			r = jsonStringResult(val,id);
		} else if ( meth == "millinow"  ) {
			r = jsonIntResult(MilliNow(),id);
		} else if ( meth == "name"  ) {
		    std::string nm = CopyFFString16((const char *)(vizlet_plugininfo().GetPluginInfo()->PluginName));
			r = jsonStringResult(nm,id);
		} else if ( meth == "dllpathname"  ) {
			r = jsonStringResult(dll_pathname(),id);
		} else {
			// If not one of the standard vizlet APIs, call the vizlet-plugin-specific API processor
			r = processJson(meth,params,id);
		}
	} catch (NosuchException& e) {
		std::string s = NosuchSnprintf("NosuchException in executeJson!! - %s",e.message());
		r = error_json(-32000,s.c_str(),id);
	} catch (...) {
		// This doesn't seem to work - it doesn't seem to catch other exceptions...
		std::string s = NosuchSnprintf("Some other kind of exception occured in executeJson!?");
		r = error_json(-32000,s.c_str(),id);
	}
	return r;
}

std::string Vizlet::submitJsonForProcessing(std::string method, cJSON *params, const char *id) {

	// We want JSON requests to be interpreted in the main thread of the FFGL plugin,
	// so we stuff the request into json_* variables and wait for the main thread to
	// pick it up (in ProcessOpenGL)
	NosuchLock(&json_mutex,"json");

	json_pending = true;
	json_method = std::string(method);
	json_params = params;
	json_id = id;

	bool err = false;
	while ( json_pending ) {
		DEBUGPRINT2(("####### Waiting for json_cond! MilliNow()=%d",MilliNow()));
		int e = pthread_cond_wait(&json_cond, &json_mutex);
		if ( e ) {
			DEBUGPRINT2(("####### ERROR from pthread_cond_wait e=%d now=%d",e,MilliNow()));
			err = true;
			break;
		}
	}
	if ( err ) {
		json_result = error_json(-32000,"Error waiting for json!?");
	}

	NosuchUnlock(&json_mutex,"json");

	return json_result.c_str();
}

AllVizParams*
Vizlet::findAllVizParams(std::string cachename) {
	std::map<std::string,AllVizParams*>::iterator it = _paramcache.find(cachename);
	if ( it == _paramcache.end() ) {
		return NULL;
	}
	return it->second;
}

AllVizParams*
Vizlet::getAllVizParams(std::string configname) {
	std::string path = NosuchConfigPath("params/"+configname+".json");
	std::map<std::string,AllVizParams*>::iterator it = _paramcache.find(path);
	if ( it == _paramcache.end() ) {
		std::string err;
		cJSON* json = jsonReadFile(path,err);
		if ( !json ) {
			DEBUGPRINT(("Unable to load vizlet params: path=%s, err=%s",
				path.c_str(),err.c_str()));
			return NULL;
		}
		AllVizParams* s = new AllVizParams(false);
		s->loadJson(json);
		// XXX - should json be freed, here?
		_paramcache[path] = s;
		return s;
	} else {
		return it->second;
	}
}

VizSprite*
Vizlet::makeAndAddVizSprite(AllVizParams* p, NosuchPos pos) {
		VizSprite* s = makeAndInitVizSprite(p,pos);
		AddVizSprite(s);
		return s;
}

VizSprite*
Vizlet::makeAndInitVizSprite(AllVizParams* p, NosuchPos pos) {

	VizSprite* s = VizSprite::makeVizSprite(p);
	s->frame = FrameSeq();

	double movedir;
	if ( p->movedirrandom.get() ) {
		movedir = 360.0f * ((double)(rand()))/ RAND_MAX;
	} else if ( p->movefollowcursor.get() ) {
		// eventually, keep track of cursor movement direction
		// for the moment it's random
		movedir = 360.0f * ((double)(rand()))/ RAND_MAX;
	} else {
		movedir = p->movedir.get();
	}

	DEBUGPRINT1(("makeAndAddVizSprite pos=%.4f   %.4f   %.4f",pos.x,pos.y,pos.z));
	s->initVizSpriteState(MilliNow(),Handle(),pos,movedir);
	// AddVizSprite(s);
	return s;
}

NosuchColor
Vizlet::channelColor(int ch) {
	double hue = (ch * 360.0f) / 16.0f;
	return NosuchColor(hue,0.5,1.0);
}

double Vizlet::movedirDegrees(AllVizParams* p) {

	if ( p->movedirrandom.get() ) {
		double f = ((double)(rand()))/ RAND_MAX;
		return f * 360.0f;
	}
	if ( p->movefollowcursor.get() ) {
		// eventually, keep track of cursor movement direction
		// for the moment it's random
		double f = ((double)(rand()))/ RAND_MAX;
		return f * 360.0f;
	}
	return p->movedir.get();
}

VizSprite* Vizlet::defaultMidiVizSprite(MidiMsg* m) {

	int minpitch = 0;
	int maxpitch = 127;
	VizSprite* s = NULL;

	if ( m->MidiType() == MIDI_NOTE_ON ) {
		if ( m->Velocity() == 0 ) {
			DEBUGPRINT(("Vizlet1 sees noteon with 0 velocity, ignoring"));
			return s;
		}
		NosuchPos pos;
		pos.x = 0.9f;
		pos.y = (m->Pitch()-minpitch) / float(maxpitch-minpitch);
		pos.z = (m->Velocity()*m->Velocity()) / (128.0*128.0);

		_defaultmidiparams->shape.set("circle");

		double fadetime = 5.0;
		_defaultmidiparams->movedir.set(180.0);
		_defaultmidiparams->speedinitial.set(0.2);
		_defaultmidiparams->sizeinitial.set(0.1);
		_defaultmidiparams->sizefinal.set(0.0);
		_defaultmidiparams->sizetime.set(fadetime);

		_defaultmidiparams->lifetime.set(fadetime);

		// control color with channel
		NosuchColor clr = channelColor(m->Channel());
		double hue = clr.hue();

		_defaultmidiparams->alphainitial.set(1.0);
		_defaultmidiparams->alphafinal.set(0.0);
		_defaultmidiparams->alphatime.set(fadetime);

		_defaultmidiparams->hueinitial.set(hue);
		_defaultmidiparams->huefinal.set(hue);
		_defaultmidiparams->huefillinitial.set(hue);
		_defaultmidiparams->huefillfinal.set(hue);

		s = makeAndAddVizSprite(_defaultmidiparams, pos);
	}
	return s;
}