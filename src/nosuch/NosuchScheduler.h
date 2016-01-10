#ifndef _NOSUCHSCHEDULER_H
#define _NOSUCHSCHEDULER_H

typedef int click_t;

#include "NosuchUtil.h"
#include "NosuchMidi.h"
#include "NosuchJSON.h"
#include "porttime.h"
#include "NosuchGraphics.h"
#include <pthread.h>
#include <list>
#include <map>
#include <algorithm>

#define OUT_QUEUE_SIZE 1024

// Session IDs (TUIO cursor sessions) and Loop IDs need to be distinct,
// since these IDs are attached to scheduled events so we can know
// who was responsible for creating the events.

// Loop IDs are session IDs that are >= LOOPID_BASE.
// Each region has one loop.
#define LOOPID_BASE 1000000
#define LOOPID_OF_REGION(id) (LOOPID_BASE+id)

extern int SchedulerCount;
extern click_t GlobalClick;
extern int GlobalPitchOffset;
extern bool NoMultiNotes;
extern bool LoopCursors;

class NosuchScheduler;
class MidiMsg;
class MidiPhrase;

class ClickListener {
public:
	virtual void processAdvanceClickTo(int click) = 0;
};

class NosuchMidiListener {
public:
	virtual void processMidiMsg(MidiMsg* mm) = 0;
};

class NosuchMidiInputListener {
public:
	virtual void processMidiInput(MidiMsg* mm) = 0;
};

class NosuchMidiOutputListener {
public:
	virtual void processMidiOutput(MidiMsg* mm) = 0;
};

class NosuchCursorMotion {
public:
	NosuchCursorMotion(int downdragup, NosuchVector pos, float depth) {
		m_downdragup = downdragup;
		m_pos = pos;
		m_depth = depth;
	}
	int m_downdragup;
	NosuchVector m_pos;
	float m_depth;
};

class SchedEvent {
public:
	enum Type {
		MIDIMSG,
		CURSORMOTION,
		MIDIPHRASE
	};
	SchedEvent(MidiMsg* m, click_t c, void* h = 0) {
		m_eventtype = SchedEvent::MIDIMSG;
		u.midimsg = m;
		click = c;
		handle = h;
		m_created = GlobalClick;
	}
	SchedEvent(MidiPhrase* ph, click_t c, void* h = 0) {
		m_eventtype = SchedEvent::MIDIPHRASE;
		u.midiphrase = ph;
		click = c;
		handle = h;
		m_created = GlobalClick;
	}
	SchedEvent(NosuchCursorMotion* cm,click_t c, void* h = 0) {
		m_eventtype = SchedEvent::CURSORMOTION;
		u.midimsg = NULL;
		u.cursormotion = cm;
		click = c;
		handle = h;
		m_created = GlobalClick;
	}
	virtual ~SchedEvent();
	std::string DebugString();

	click_t click;	// relative when in loops, absolute elsewhere
	union {
		MidiMsg* midimsg;
		MidiPhrase* midiphrase;
		NosuchCursorMotion* cursormotion;
	} u;

	int eventtype() { return m_eventtype; }
	click_t created() { return m_created; }

protected:
	void* handle;	// handle to thing that created it
	friend class NosuchScheduler;

private:

	int m_eventtype;
	click_t m_created;	// absolute
};

typedef std::list<SchedEvent*> SchedEventList;
typedef std::list<SchedEvent*>::iterator SchedEventIterator;

class NosuchScheduler {
public:
	NosuchScheduler() {
		DEBUGPRINT1(("NosuchScheduler CONSTRUCTED!!, count=%d",SchedulerCount++));
		m_running = false;
		m_currentclick = 0;
		m_timestamp = 0;

#ifdef NOWPLAYING
		m_nowplaying_note.clear();
		// m_nowplaying_controller.clear();
#endif

		SetClicksPerSecond(192);

		m_clicks_per_clock = 4;
		NosuchLockInit(&m_scheduled_mutex,"scheduled");
		NosuchLockInit(&m_notesdown_mutex,"notesdown");
		NosuchLockInit(&m_queue_mutex,"queue");
		m_midioutput_client = NULL;

		m_midi_input_stream = std::vector<PmStream*>();
		m_midi_input_name = std::vector<std::string>();

		m_midi_output_stream = std::vector<PmStream*>();
		m_midi_output_name = std::vector<std::string>();

		m_midi_merge_outport = std::vector<int>();
		m_midi_merge_name = std::vector<std::string>();

		m_midiinput_client = NULL;
		m_midioutput_client = NULL;

		m_click_client = NULL;
		m_periodic_ANO = false;
		m_queue = new SchedEventList();
	}
	virtual ~NosuchScheduler() {
		Stop();
	}

	const char* MidiInputName(size_t n)  {
		if (n>=m_midi_input_name.size()) {
			return NULL;
		}
		return m_midi_input_name[n].c_str();
	}

	const char* MidiOutputName(size_t n)  {
		if (n>=m_midi_output_name.size()) {
			return NULL;
		}
		return m_midi_output_name[n].c_str();
	}

	const char* MidiMergeName(size_t n)  {
		if (n>=m_midi_merge_name.size()) {
			return NULL;
		}
		return m_midi_merge_name[n].c_str();
	}

	static click_t ClicksPerSecond();
	static void SetClicksPerSecond(click_t clkpersec);
	static void SetTempoFactor(float f);

	void SetClickProcessor(ClickListener* client) {
		m_click_client = client;
	}
	void SetMidiInputListener(NosuchMidiListener* client) {
		m_midiinput_client = client;
	}
	void SetMidiOutputListener(NosuchMidiListener* client) {
		m_midioutput_client = client;
	}
	bool StartMidi(cJSON *config);
	void Stop();
	void AdvanceTimeAndDoEvents(PtTimestamp timestamp);
	void Callback(PtTimestamp timestamp);
	std::string DebugString();

	// int NumberScheduled(click_t minclicks, click_t maxclicks, std::string sid);
	bool AnythingPlayingAt(click_t clk, void* handle);
	void IncomingNoteOff(click_t clk, int ch, int pitch, int vel, void* handle);
	void IncomingMidiMsg(MidiMsg* m, click_t clk, void* handle);

	SchedEventList* ScheduleOf(void* handle);
	void ScheduleMidiMsg(MidiMsg* m, click_t clk, void* handle);
	void ScheduleMidiPhrase(MidiPhrase* m, click_t clk, void* handle);
	void ScheduleClear();
	bool ScheduleAddEvent(SchedEvent* e, bool lockit=true);  // returns false if not added, i.e. means caller should free it

	void QueueMidiMsg(MidiMsg* m, click_t clk);
	void QueueMidiPhrase(MidiPhrase* m, click_t clk);
	void QueueClear();
	bool QueueAddEvent(SchedEvent* e);  // returns false if not added, i.e. means caller should free it

	void SendPmMessage(PmMessage pm, PmStream* ps, void* handle);
	void SendMidiMsg(MidiMsg* mm, void* handle);
	void SendControllerMsg(MidiMsg* m, void* handle, bool smooth);  // gives ownership of m away
	void SendPitchBendMsg(MidiMsg* m, void* handle, bool smooth);  // gives ownership of m away
	void ANO(int psi = -1, int ch = -1);
	void ANO(PmStream* ps, int ch = -1);
	void setPeriodicANO(bool b) { m_periodic_ANO = b; }

	static int m_timestamp;
	static int m_ClicksPerSecond;
	static double m_ClicksPerMillisecond;
	static int m_LastTimeStamp;
	static int m_timestamp0;
	static click_t m_currentclick;

	click_t CurrentClick() { return m_currentclick; }

	std::list<MidiMsg*>& NotesDown() {
		return m_notesdown;
	}

	void ClearNotesDown() {
		LockNotesDown();
		m_notesdown.clear();
		UnlockNotesDown();
	}

	void LockNotesDown() { NosuchLock(&m_notesdown_mutex,"notesdown"); }
	void UnlockNotesDown() { NosuchUnlock(&m_notesdown_mutex,"notesdown"); }

	void LockQueue() { NosuchLock(&m_queue_mutex,"queue"); }
	void UnlockQueue() { NosuchUnlock(&m_queue_mutex,"queue"); }

private:

	PmStream* _openMidiInput(std::string input);
	PmStream* _openMidiOutput(std::string output);

	void _maintainNotesDown(MidiMsg* m);
	std::list<MidiMsg*> m_notesdown;

	int TryLockScheduled() { return NosuchTryLock(&m_scheduled_mutex,"sched"); }
	void LockScheduled() { NosuchLock(&m_scheduled_mutex,"sched"); }
	void UnlockScheduled() { NosuchUnlock(&m_scheduled_mutex,"sched"); }

	void _addQueueToScheduled();

	void _sortEvents(SchedEventList* sl);
	void DoEventAndDelete(SchedEvent* e, void* handle);
	void DoMidiMsgEvent(MidiMsg* m, void* handle);
	bool m_running;
	int m_clicks_per_clock;

	// Per-handle scheduled events
	std::map<void*,SchedEventList*> m_scheduled;
	SchedEventList* m_queue;  // Things to be added to _scheduled

#ifdef NOWPLAYING
	// This is a mapping of session id (either TUIO session id or Looping id)
	// to whatever MIDI message (i.e. notes) are currently active (and need a
	// noteoff if we change).
	std::map<int,MidiMsg*> m_nowplaying_note;

	// This is a mapping of session id (either TUIO session id or Looping id)
	// to whatever the last MIDI controllers were.  The
	// map inside the map is a mapping of controller numbers
	// to the messages.
	std::map<int,std::map<int,MidiMsg*>> m_nowplaying_controller;

	// This is a mapping of session id (either TUIO session id or Looping id)
	// to whatever the last pitchbend was.
	std::map<int,MidiMsg*> m_nowplaying_pitchbend;
#endif

	std::vector<PmStream *> m_midi_input_stream;
	std::vector<std::string> m_midi_input_name;

	std::vector<PmStream *> m_midi_output_stream;
	std::vector<std::string> m_midi_output_name;

	std::vector<int> m_midi_merge_outport;
	std::vector<std::string> m_midi_merge_name;

	ClickListener* m_click_client;
	NosuchMidiListener*	m_midioutput_client;
	NosuchMidiListener*	m_midiinput_client;

	pthread_mutex_t m_scheduled_mutex;
	pthread_mutex_t m_notesdown_mutex;
	pthread_mutex_t m_queue_mutex;
	bool m_periodic_ANO;
};

#endif
