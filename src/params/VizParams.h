#ifndef _VIZPARAMS_H
#define _VIZPARAMS_H

#include <vector>
#include <map>
#include "NosuchException.h"
#include "NosuchJson.h"

std::string JsonAllValues();
std::string JsonAllTypes();

#define IntString(x) NosuchSnprintf("%d",x)
#define DoubleString(x) NosuchSnprintf("%f",x)
#define BoolString(x) NosuchSnprintf("%s",x?"on":"off")

typedef std::vector<std::string> StringList;

int string2int(std::string s);
float string2double(std::string s);
bool string2bool(std::string s);

class DoubleParam {
public:
	DoubleParam() { enabled = false; }
	double get() { return value; }
	void set(double v) { value = v; enabled = true; }
	void set(cJSON* j) {
		if (j->type == cJSON_String) {
			set(atof(j->valuestring));
		}
		else if (j->type == cJSON_Number) {
			set(j->valuedouble);
		}
		else {
			throw NosuchException("Unable to set DoubleParam from j->type=%d", j->type);
		}
	}
	void unset() { enabled = false; }
	operator double() { return value; }
	bool isset() { return enabled; }
private:
	double value;
	bool enabled;
};

class BoolParam {
public:
	BoolParam() { enabled = false; }
	bool get() { return value; }
	void set(bool v) { value = v; enabled = true; }
	void set(cJSON* j) {
		switch (j->type) {
		case cJSON_True: set(true); break;
		case cJSON_False: set(false); break;
		case cJSON_String: set(jsonIsTrueValue(j->valuestring)); break;
		case cJSON_Number: set(j->valueint != 0); break;
		default:
			throw NosuchException("Unable to set BoolParam from j->type=%d", j->type);
		}
	}
	operator bool() { return value; }
	bool isset() { return enabled; }
private:
	bool value;
	bool enabled;
};

class StringParam {
public:
	StringParam() { enabled = false; }
	std::string get() { return value; }
	void set(std::string v) { value = v; enabled = true; }
	void set(cJSON* j) {
		if (j->type == cJSON_Number) {
			if ((j->valuedouble - j->valueint) != 0.0) {
				set(NosuchSnprintf("%f", j->valuedouble));
			}
			else {
				set(NosuchSnprintf("%d", j->valueint));
			}
		}
		else if (j->type == cJSON_String) {
			set(j->valuestring);
		}
		else {
			throw NosuchException("Unable to set StringParam from j->type=%d", j->type);
		}
	}
	operator std::string() { return value; }
	bool isset() { return enabled; }
private:
	std::string value;
	bool enabled;
};

class IntParam {
public:
	IntParam() { enabled = false; }
	int get() { return value; }
	void set(int v) { value = v; enabled = true; }
	void set(cJSON* j) {
		if (j->type == cJSON_Number) {
			set(j->valueint);
		}
		else if (j->type == cJSON_String) {
			set(atoi(j->valuestring));
		}
		else {
			throw NosuchException("Unable to set IntParam from j->type=%d", j->type);
		}
	}
	operator int() { return value; }
	bool isset() { return enabled; }
private:
	int value;
	bool enabled;
};

class VizParams {

public:

	static std::map<std::string, StringList> StringVals;

	static bool Initialized;
	static void Initialize() {

		if (Initialized) {
			return;
		}

		Initialized = true;

		// Eventually, all this initialization should be autogenerated
		// by genparams.py, based on the contents of AllVizParams.h

		StringVals["shapeTypes"] = StringList();
		StringVals["shapeTypes"].push_back("nothing");  // make sure that index 0 is nothing?
		StringVals["shapeTypes"].push_back("line");
		StringVals["shapeTypes"].push_back("triangle");
		StringVals["shapeTypes"].push_back("square");
		StringVals["shapeTypes"].push_back("arc");
		StringVals["shapeTypes"].push_back("circle");
		StringVals["shapeTypes"].push_back("outline");

		StringVals["mirrorTypes"] = StringList();
		StringVals["mirrorTypes"].push_back("none");
		StringVals["mirrorTypes"].push_back("vertical");
		StringVals["mirrorTypes"].push_back("horizontal");
		StringVals["mirrorTypes"].push_back("four");

		StringVals["logicTypes"] = StringList();
		StringVals["logicTypes"].push_back("none");
		StringVals["logicTypes"].push_back("vertical");
		StringVals["logicTypes"].push_back("horizontal");
		StringVals["logicTypes"].push_back("outline");

		StringVals["behaviourTypes"] = StringList();
		StringVals["behaviourTypes"].push_back("default");
		StringVals["behaviourTypes"].push_back("museum");
		StringVals["behaviourTypes"].push_back("STEIM");
		StringVals["behaviourTypes"].push_back("casual");
		StringVals["behaviourTypes"].push_back("burn");

		StringVals["controllerTypes"] = StringList();
		StringVals["controllerTypes"].push_back("modulationonly");
		StringVals["controllerTypes"].push_back("allcontrollers");
		StringVals["controllerTypes"].push_back("pitchYZ");

		StringVals["movedirTypes"] = StringList();
		StringVals["movedirTypes"].push_back("cursor");
		StringVals["movedirTypes"].push_back("left");
		StringVals["movedirTypes"].push_back("right");
		StringVals["movedirTypes"].push_back("up");
		StringVals["movedirTypes"].push_back("down");
		StringVals["movedirTypes"].push_back("random");

		StringVals["rotangdirTypes"] = StringList();
		StringVals["rotangdirTypes"].push_back("right");
		StringVals["rotangdirTypes"].push_back("left");
		StringVals["rotangdirTypes"].push_back("random");

		StringVals["soundTypes"] = StringList();
		StringVals["soundTypes"].push_back("Bass1");
		StringVals["soundTypes"].push_back("Bass2");
		StringVals["soundTypes"].push_back("Synth1");
		StringVals["soundTypes"].push_back("Synth2");
		StringVals["soundTypes"].push_back("Drum1");
		StringVals["soundTypes"].push_back("Drum2");
	}

	virtual std::string GetAsString(std::string) = 0;
	virtual std::string GetType(std::string) = 0;
	virtual std::string MinValue(std::string) = 0;
	virtual std::string MaxValue(std::string) = 0;
	virtual std::string DefaultValue(std::string) = 0;

	double adjust(double v, double amount, double vmin, double vmax) {
		v += amount*(vmax - vmin);
		if (v < vmin)
			v = vmin;
		else if (v > vmax)
			v = vmax;
		return v;
	}
	int adjust(int v, double amount, int vmin, int vmax) {
		int incamount = (int)(amount*(vmax - vmin));
		if (incamount == 0) {
			incamount = (amount > 0.0) ? 1 : -1;
		}
		v = (int)(v + incamount);
		if (v < vmin)
			v = vmin;
		else if (v > vmax)
			v = vmax;
		return v;
	}
	bool adjust(bool v, double amount) {
		if (amount > 0.0) {
			return true;
		}
		if (amount < 0.0) {
			return false;
		}
		// if amount is 0.0, no change.
		return v;
	}
	std::string adjust(std::string v, double amount, std::vector<std::string>& vals) {
		// Find the existing value
		int existing = -1;
		int sz = vals.size();
		if (sz == 0) {
			throw NosuchException("vals array is empty!?");
		}
		for (int ei = 0; ei < sz; ei++) {
			if (v == vals[ei]) {
				existing = ei;
				break;
			}
		}
		if (existing < 0) {
			existing = 0;
		}
		// Return the next or previous value in the list
		int i = existing + ((amount > 0.0) ? 1 : -1);
		if (i < 0) {
			i = sz - 1;
		}
		if (i >= sz) {
			i = 0;
		}
		return vals[i];
	}
	std::string _JsonListOfValues(char* names[]) {
		std::string s = "{";
		std::string sep = "";
		for (char** nm = names; *nm; nm++) {
			s += (sep + "\"" + *nm + "\": \"" + GetAsString(*nm) + "\"");
			sep = ",";
		}
		s += "}";
		return s;
	}
	std::string _JsonListOfStringValues(std::string type) {
		StringList vals = StringVals[type];
		std::string s = "[";
		std::string sep = "";
		for (std::vector<std::string>::iterator it = vals.begin(); it != vals.end(); ++it) {
			s += (sep + "\"" + *it + "\"");
			sep = ",";
		}
		s += "]";
		return s;
	}
	std::string _JsonListOfParams(char* names[]) {

		std::string s = "{ ";
		std::string sep = "";
		int n = 0;
		for (char** pnm = names; *pnm; pnm++) {
			char* nm = *pnm;
			std::string t = GetType(nm);
			std::string mn = MinValue(nm);
			std::string mx = MaxValue(nm);
			std::string dflt = DefaultValue(nm);
			s += (sep + "\"" + nm + "\": ");
			s += "{ \"type\": \"" + t + "\", \"min\": \"" + mn + "\", \"max\": \"" + mx + "\", \"default\": \"" + dflt + "\" }";
			sep = ",";
		}
		s += "}";
		return s;
	}
};

#endif