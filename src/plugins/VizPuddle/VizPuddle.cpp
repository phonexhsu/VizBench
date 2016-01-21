#include "Vizlet.h"
#include "VizPuddle.h"

static CFFGLPluginInfo PluginInfo ( 
	VizPuddle::CreateInstance,	// Create method
	"V018",		// Plugin unique ID
	"VizPuddle",	// Plugin name	
	1,			// API major version number
	000,		// API minor version number	
	1,			// Plugin major version number
	000,		// Plugin minor version number
	FF_EFFECT,	// Plugin type
	"VizPuddle: a sample visual synth",	// description
	"by Tim Thompson - me@timthompson.com" 			// About
);

std::string vizlet_name() { return "VizPuddle"; }
CFFGLPluginInfo& vizlet_plugininfo() { return PluginInfo; }
void vizlet_setdll(std::string dll) { }

VizPuddle::VizPuddle() : Vizlet() {

	DEBUGPRINT(("VizPuddle is being created and initialized! this=%ld",(long)this));

	const char* regionnames[] = { "UPPER", "LOWER", "LEFT", "RIGHT", NULL };
	const char* buttonnames[] = {	"UL1", "UL2", "UL3", "UR1", "UR2", "UR3",
									"LL1", "LL2", "LL3", "LR1", "LR2", "LR3",
									NULL
									};

	const char* s;
	for (int i = 0; (s=regionnames[i])!=NULL; i++ ) {
		_region[s] = new Region(s);
	}
	for (int i = 0; (s = buttonnames[i]) != NULL; i++) {
		_button[s] = new Button(s);
	}
	for (int i = 0; i < MAX_MIDI_PORTS; i++) {
		m_porthandle[i] = _strdup(NosuchSnprintf("port%d", i).c_str());
	}
	_autoloadparams = true;
}

VizPuddle::~VizPuddle() {
}

DWORD __stdcall VizPuddle::CreateInstance(CFreeFrameGLPlugin **ppInstance) {
	*ppInstance = new VizPuddle();
	return (*ppInstance != NULL)? FF_SUCCESS : FF_FAIL;
}

void VizPuddle::processCursor(VizCursor* c, int downdragup) {
	// NO OpenGL calls here
	DEBUGPRINT1(("VizPuddle::processCursor! downdragup=%d c=%.4f %.4f",downdragup,c->pos.x,c->pos.y));
	// palette()->processCursor(c,downdragup);
	if (downdragup == CURSOR_DOWN) {
#ifdef WHEN_ON_SPACE_PALETTE
		int sid = c->sid;
		std::string pipeline = "";
		// See if it's a button
		for (const auto &pair : _button ) {
			if (sid >= pair.second->sid_min && sid <= pair.second->sid_max) {
				pipeline = pair.second->pipeline;
				break;
			}
		}
		if (pipeline != "") {
			LoadPipeline(pipeline);
		}
#endif
	}

	SpriteVizParams *sp = NULL;
	MidiVizParams *mp = NULL;
	// Look through the regions to find the one that matches the cursor's sid
	int sid = c->sid;
	for (const auto &pair : _region ) {
		if (sid >= pair.second->sid_min && sid <= pair.second->sid_max) {
			sp = pair.second->spriteparams;
			mp = pair.second->midiparams;
			break;
		}
	}
	if (sp) {
		_cursorSprite(c, downdragup, sp);
	}
	if (mp) {
		_cursorMidi(c, downdragup, mp);
	}
}

std::string VizPuddle::processJson(std::string meth, cJSON *json, const char *id) {
	// NO OpenGL calls here

	DEBUGPRINT1(("VizPuddle::processJson puddle=%ld meth=%s",(long)this,meth.c_str()));
	if (meth == "apis") {
		std::string apis = "";
		for (const auto &pair : _region) {
			std::string nm = pair.first;
				apis += "set_" + nm + "_sidrange(sidrange);";
				apis += "set_" + nm + "_sprite(paramfile);";
				apis += "set_" + nm + "_midi(paramfile);";
		}
		for (const auto &pair : _button) {
			std::string nm = pair.first;
				apis += "set_" + nm + "_sidrange(sidrange);";
				apis += "set_" + nm + "_pipeline(pipeline);";
		}
		apis += "set_autoloadparams(onoff);";
		apis += "testapi();";
		return jsonStringResult(apis, id);
	}

	if (meth == "testapi") {
		DEBUGPRINT(("VizPuddle.testapi called!"));
		return jsonOK(id);
	}

	if (meth == "dump") {
		std::string dump = "[";
		std::string sep = "";
		for (const auto &pair : _region) {
			dump += sep + NosuchSnprintf("{\"method\":\"set_%s_sidrange\",\"params\":{\"sidrange\":\"%d-%d\"}}", pair.first.c_str(), pair.second->sid_min, pair.second->sid_max);
			sep = ",";
			dump += sep + NosuchSnprintf("{\"method\":\"set_%s_sprite\",\"params\":{\"paramfile\":\"%s\"}}", pair.first.c_str(), pair.second->spriteparamfile.c_str());
			dump += sep + NosuchSnprintf("{\"method\":\"set_%s_midi\",\"params\":{\"paramfile\":\"%s\"}}", pair.first.c_str(), pair.second->midiparamfile.c_str());
		}
		for (const auto &pair : _button) {
			dump += sep + NosuchSnprintf("{\"method\":\"set_%s_sidrange\",\"params\":{\"sidrange\":\"%d-%d\"}}", pair.first.c_str(), pair.second->sid_min, pair.second->sid_max);
			dump += sep + NosuchSnprintf("{\"method\":\"set_%s_pipeline\",\"params\":{\"pipeline\":\"%s\"}}", pair.first.c_str(), pair.second->pipeline.c_str());
		}
		dump += "]";
		return jsonStringResult(dump, id);
	}

	// Here we go through all the region names and look for their set/get methods
	for (const auto &pair : _region) {
		std::string nm = pair.first;
		Region* r = pair.second;

		std::string result;

		if (meth == "set_" + nm + "_sidrange") { result = _set_sidrange(r, json, id); }
		if (meth == "get_" + nm + "_sidrange") { result = jsonStringResult(NosuchSnprintf("%d-%d", r->sid_min, r->sid_max), id); }

		if (meth == "set_" + nm + "_sprite") { result = _set_region_spriteparams(r, json, id); }
		if (meth == "get_" + nm + "_sprite") { result = jsonStringResult(r->spriteparamfile, id); }

		if (meth == "set_" + nm + "_midi") { result = _set_region_midiparams(r, json, id); }
		if (meth == "get_" + nm + "_midi") { result = jsonStringResult(r->midiparamfile, id); }

		if (result != "") {
			const char *p = result.c_str();
			if (p[0] != '{') {
				DEBUGPRINT(("Bad result in meth=%s result=%s", meth.c_str(), p));
			}
			return result;
		}
	}

	// Here we go through all the button names and look for their set/get methods
	for (const auto &pair : _button) {
		std::string nm = pair.first;
		Button* b = pair.second;

		std::string result;

		if (meth == "set_" + nm + "_sidrange") { result = _set_sidrange(b, json, id); }
		if (meth == "get_" + nm + "_sidrange") { result = jsonStringResult(NosuchSnprintf("%d-%d", b->sid_min, b->sid_max), id); }

		if (meth == "set_" + nm + "_pipeline") { result = _set_button_pipeline(b, json, id); }
		if (meth == "get_" + nm + "_pipeline") { result = jsonStringResult(b->pipeline, id); }

		if (result != "") {
			const char *p = result.c_str();
			if (p[0] != '{') {
				DEBUGPRINT(("Bad result in meth=%s result=%s", meth.c_str(), p));
			}
			return result;
		}
	}

	// PARAMETER "autoloadparams"
	if (meth == "set_autoloadparams") {
		_autoloadparams = jsonNeedBool(json, "onoff", false);
		return jsonOK(id);
	}
	if (meth == "get_autoloadparams") {
		return jsonIntResult(_autoloadparams ? 1 : 0, id);
	}

	throw NosuchException("VizPuddle - Unrecognized method '%s'",meth.c_str());
}

void
VizPuddle::_reloadParams(Region* r) {
	SpriteVizParams* p;
	p = checkSpriteVizParamsAndLoadIfModifiedSince(r->spriteparamfile, r->lastspritefilecheck, r->lastspritefileupdate);
	if (p) {
		r->spriteparams = p;
	}
	MidiVizParams* m;
	m = checkMidiVizParamsAndLoadIfModifiedSince(r->midiparamfile, r->lastmidifilecheck, r->lastmidifileupdate);
	if (m) {
		r->midiparams = m;
	}
}

bool VizPuddle::processDraw() {
	// OpenGL calls here
	if (_autoloadparams) {
		for (const auto &pair : _region ) {
			Region* r = pair.second;
			_reloadParams(r);
		}
	}
	DrawVizSprites();
	return true;
}

int VizPuddle::_channelOf(VizCursor* c, MidiVizParams* mp) {
	return mp->channel;
}

int _interpolate(double f, int minval, int maxval) {
	// The (dval+1) in the next expression is intended to make the
	// 0 to 1.0 range of f map evenly to the entire value range,
	// otherwise the maxval would only be generated for exactly 1.0.
	int dval = maxval - minval;
	int r = (int)(f * (dval+1));
	if (r > dval) {
		r = dval;
	}
	return minval + r;
}

int VizPuddle::_pitchOf(VizCursor* c, MidiVizParams* mp) {

	int p = _interpolate(c->pos.x, mp->pitchmin, mp->pitchmax);
	std::string scale = mp->scale.get();
	Scale s = Scale::Scales[scale];
	p = s.closestTo(p);
	return p;
}

int VizPuddle::_velocityOf(VizCursor* c) {
	return 100;
}

// A "beat" is a quarter note, typically
click_t VizPuddle::_clicksPerBeat() {
	// Might want to cache this?
	int bpm = 120;
	return SchedulerClicksPerSecond() * 60 / bpm;
}

click_t VizPuddle::_durationOf(VizCursor* c) {
	return _clicksPerBeat()/2;
}

click_t VizPuddle::_quantOf(VizCursor* c) {
	double f = 1.0;
	// Slower/larger quantization toward the bottom.
#define TIMEFRETS_3
#ifdef TIMEFRETS_4
	if (c->pos.y < 0.25) {
		f = 1.0;
	}
	else if (c->pos.y < 0.5) {
		f = 0.5;
	}
	else if (c->pos.y < 0.75) {
		f = 0.25;
	}
	else {
		f = 0.125;
	}
#endif
#ifdef TIMEFRETS_3
	if (c->pos.y < 0.33) {
		f = 0.5;
	}
	else if (c->pos.y < 0.66) {
		f = 0.25;
	}
	else {
		f = 0.125;
	}
#endif
	click_t q = (click_t)(f * _clicksPerBeat());  // round?
	return q;
}

click_t VizPuddle::_quantizeToNext(click_t tm, click_t q) {
	return tm - (tm%q) + q;
}

void VizPuddle::_cursorMidi(VizCursor* c, int downdragup, MidiVizParams* mp) {

	// the MidiVizParams is region-specific
	if (mp->arpeggiate.get()) {
		_cursorArpeggiatedMidi(c, downdragup, mp);
	}
	else {
		_cursorNormalMidi(c, downdragup, mp);
	}
	return;
}

void VizPuddle::_cursorArpeggiatedMidi(VizCursor* c, int downdragup, MidiVizParams* mp) {

	MidiPhrase *ph = new MidiPhrase();
	int ch = _channelOf(c,mp);
	int pitch = _pitchOf(c,mp);
	int vel = _velocityOf(c);
	click_t dur = _durationOf(c);
	click_t quant = _quantOf(c);
	click_t now = SchedulerCurrentClick();
	int outport = mp->port.get();

	MidiNoteOn *noteon = MidiNoteOn::make(ch, pitch, vel);
	MidiNoteOff *noteoff = MidiNoteOff::make(ch, pitch, 0);
	ph->insert(noteon, 0);
	ph->insert(noteoff, dur);
	ph->SetInputPort(MIDI_PORT_OF_GENERATED_STUFF);
	ph->SetOutputPort(outport);
	click_t tm = _quantizeToNext(now, quant);

	if (outport >= MAX_MIDI_PORTS) {
		throw NosuchException("port value (%d) is too large!?",outport);
	}
	// The handle is per-port
	QueueMidiPhrase(ph, tm, m_porthandle[outport]);
}

int _outputPort(MidiVizParams* mp) {
	int outport = mp->port.get();
	if (outport >= MAX_MIDI_PORTS) {
		throw NosuchException("port value (%d) is too large!?", outport);
	}
	return outport;
}

void VizPuddle::_cursorNormalMidi(VizCursor* c, int downdragup, MidiVizParams* mp) {

	// In "NormalMidi" mode, the noteon/noteoff's are generated separately, so that
	// if you hold a cursor down, the note will stay on until you release the cursor
	// or move to a cursor position assigned to a different pitch.

	int ch = _channelOf(c,mp);
	int pitch = _pitchOf(c, mp);
	int vel = _velocityOf(c);
	click_t dur = _durationOf(c);
	click_t quant = _quantOf(c);
	int outport = _outputPort(mp);

	click_t now = SchedulerCurrentClick();
	click_t nowquant = _quantizeToNext(now, quant);

	// The first thing to do is check for CURSOR_DRAG, because if we've been dragged into a
	// position that triggers a different pitch, we're going to make it look like a CURSOR_DOWN

	if (downdragup == CURSOR_DRAG) {
		if (c->m_pending_noteoff == NULL) {
			DEBUGPRINT(("No pending_noteoff in CURSOR_DRAG!?"));
			// but keep going, to generate noteon
		}
		else {
			// See if the pitch value of the current position has changed from the
			// saved noteoff.  If it's the same, the only thing we want to do is
			// (possibly) generated a controller message from the depth.
			if (pitch == c->m_pending_noteoff->Pitch()) {
				if (mp->depthctlnum.get() <= 0) {
					DEBUGPRINT1(("depthctl <= 0, no controller msg sent"));
					return;
				}
				int dv = mp->depthctlmax - mp->depthctlmin;
				int v = _interpolate(c->pos.z, mp->depthctlmin.get(), mp->depthctlmax.get());
				v = BoundValue(v, 0, 127);
				MidiController* ctl = MidiController::make(ch, mp->depthctlnum, v);
				QueueMidiMsg(ctl, nowquant, m_porthandle[outport]);
				return;
			}
			
			// When the new pitch is different, terminate the current note
			// (i.e. send the saved noteoff) and then generate a new noteon/noteoff
			QueueMidiMsg(c->m_pending_noteoff, nowquant, m_porthandle[outport]);
			c->m_pending_noteoff = NULL;

		}
		// Pretent we've just gotten a CURSOR_DOWN event in the new position
		downdragup = CURSOR_DOWN;
		// No return here, fall through
	}

	if (downdragup == CURSOR_DOWN) {

		if (c->m_pending_noteoff) {
			DEBUGPRINT(("Hey! m_pending_noteoff isn't NULL for CURSOR_DOWN?"));
		}

		MidiNoteOn *noteon = MidiNoteOn::make(ch, pitch, vel);
		MidiNoteOff *noteoff = MidiNoteOff::make(ch, pitch, 0);

		noteon->SetInputPort(MIDI_PORT_OF_GENERATED_STUFF);
		noteon->SetOutputPort(outport);

		noteoff->SetInputPort(MIDI_PORT_OF_GENERATED_STUFF);
		noteoff->SetOutputPort(outport);

		// Send the noteon, but don't sent the noteoff until we get a CURSOR_UP

		QueueMidiMsg(noteon, nowquant, m_porthandle[outport]);
		c->m_pending_noteoff = noteoff;
	}
	else if (downdragup == CURSOR_UP) {

		if (c->m_pending_noteoff == NULL) {
			DEBUGPRINT(("Hey, no m_pending_noteoff for CURSOR_UP!?"));
		}
		else {
			// Should the noteoff time really be quantized?
			QueueMidiMsg(c->m_pending_noteoff, nowquant, m_porthandle[outport]);
			c->m_pending_noteoff = NULL;
		}
	}
	else {
		DEBUGPRINT(("Invalid value for downdragup = %d !?", downdragup));
	}

#if 0
			MidiNoteOn *noteon = MidiNoteOn::make(ch, pitch, vel);
			MidiNoteOff *noteoff = MidiNoteOff::make(ch, pitch, 0);
			ph->insert(noteon, 0);
			ph->insert(noteoff, dur);
			ph->SetInputPort(MIDI_PORT_OF_GENERATED_STUFF);
			ph->SetOutputPort(outport);
			click_t tm = _quantizeToNext(now, quant);

			// The handle is per-port
			QueueMidiPhrase(ph, tm, m_porthandle[outport]);
#endif
}

#if 0
void VizPuddle::_cursorMidi(VizCursor* c, int downdragup, MidiVizParams* mp) {

	// the MidiVizParams is region-specific

	// When not arpeggiating/repeating, only generate notes for cursor down.
	if ( ! mp->arpeggiate.get() && downdragup != CURSOR_DOWN ) {
		return;
	}

	if (downdragup == CURSOR_UP && mp->duration.get() == "hold") {
	}

	MidiPhrase *ph = new MidiPhrase();
	int ch = _channelOf(c,mp);
	int pitch = _pitchOf(c,mp);
	int vel = _velocityOf(c);
	click_t dur = _durationOf(c);
	click_t quant = _quantOf(c);
	click_t now = SchedulerCurrentClick();
	int outport = mp->port.get();

	MidiNoteOn *noteon = MidiNoteOn::make(ch, pitch, vel);
	MidiNoteOff *noteoff = MidiNoteOff::make(ch, pitch, 0);
	ph->insert(noteon, 0);
	ph->insert(noteoff, dur);
	ph->SetInputPort(MIDI_PORT_OF_GENERATED_STUFF);
	ph->SetOutputPort(outport);
	click_t tm = _quantizeToNext(now, quant);

	if (outport >= MAX_MIDI_PORTS) {
		throw NosuchException("port value (%d) is too large!?",outport);
	}
	// The handle is per-port
	QueueMidiPhrase(ph, tm, m_porthandle[outport]);
}
#endif

void VizPuddle::_cursorSprite(VizCursor* c, int downdragup, SpriteVizParams* spriteparams) {

	DEBUGPRINT1(("cursorSprite! sid=%d xyz = %.5f %.5f %.5f", c->sid, c->pos.x, c->pos.y, c->pos.z));

	NosuchPos pos = c->pos;
	double movedir = 0.0;
	bool randpos = false;
	if (randpos) {
		pos.x = pos.y = 0.5;
		// pos.z = (m->Velocity()*m->Velocity()) / (128.0*128.0);
		movedir = (double)(rand() % 360);
		spriteparams->movedir.set(movedir);
	}
	else {
		movedir = spriteparams->movedir.get();
	}

	DEBUGPRINT2(("_cursorSprite spriteparams=%ld shape=%s",(long)spriteparams,spriteparams->shape.get().c_str()));
	makeAndAddVizSprite(spriteparams, pos);
}

std::string
VizPuddle::_set_region_spriteparams(Region* r, cJSON* json, const char* id)
{
	std::string file = jsonNeedString(json, "paramfile", "");
	if (file == "") {
		return jsonError(-32000, "Bad file value", id);
	}
	if (_loadSpriteVizParamsFile(file, r)) {
		return jsonOK(id);
	}
	else {
		return jsonError(-32000, "Unable to load spriteparams file", id);
	}
}

std::string
VizPuddle::_set_sidrange(SidRangeable* b, cJSON* json, const char* id)
{
	std::string sidrange = jsonNeedString(json, "sidrange", "");
	if (sidrange == "") {
		return jsonError(-32000, "Bad sidrange value", id);
	}
	int sidmin;
	int sidmax;
	int n = sscanf(sidrange.c_str(), "%d-%d",&sidmin,&sidmax);
	if (n == 1) {
		b->sid_min = sidmin;
		b->sid_max = sidmin;
	}
	else if (n == 2) {
		b->sid_min = sidmin;
		b->sid_max = sidmax;
	}
	else {
		return jsonError(-32000, "Unable to interpret sidrange value: %s", sidrange.c_str());
	}
	return jsonOK(id);
}

std::string
VizPuddle::_set_button_pipeline(Button* b, cJSON* json, const char* id)
{
	std::string pipeline = jsonNeedString(json, "pipeline", "");
	if (pipeline == "") {
		return jsonError(-32000, "Bad pipeline value", id);
	}
	b->pipeline = pipeline;
	return jsonOK(id);
}

bool VizPuddle::_loadSpriteVizParamsFile(std::string fname, VizPuddle::Region* r) {
	r->lastspritefilecheck = time(0);
	r->lastspritefileupdate = time(0);
	SpriteVizParams* p = getSpriteVizParams(fname);
	if (!p) {
		r->spriteparamfile = "";
		r->spriteparams = NULL;
		return false;
	}
	r->spriteparamfile = fname;
	r->spriteparams = p;
	return true;
}

std::string
VizPuddle::_set_region_midiparams(Region* r, cJSON* json, const char* id)
{
	std::string file = jsonNeedString(json, "paramfile", "");
	if (file == "") {
		return jsonError(-32000, "Bad file value", id);
	}
	if (_loadMidiVizParamsFile(file, r)) {
		return jsonOK(id);
	}
	else {
		return jsonError(-32000, "Unable to load spriteparams file", id);
	}
}

bool VizPuddle::_loadMidiVizParamsFile(std::string fname, VizPuddle::Region* r) {
	r->lastmidifilecheck = time(0);
	r->lastmidifileupdate = time(0);
	MidiVizParams* p = getMidiVizParams(fname);
	if (!p) {
		r->midiparamfile = "";
		r->midiparams = NULL;
		return false;
	}
	r->midiparamfile = fname;
	r->midiparams = p;
	return true;
}