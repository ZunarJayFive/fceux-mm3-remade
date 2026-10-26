/* Mega Man 3 "Remade" album player (built into FCEUX). See mm3audio.h. */

#include "mm3audio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <algorithm>

#ifdef WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dirent.h>
#endif

#include "types.h"
#include "fceu.h"
#include "driver.h"
#include "x6502.h"
#include "git.h"
#include "cart.h"

#include "mm3audio_data.inc"

namespace {

// ---------------------------------------------------------------- data

struct Track {
	std::string num;
	std::vector<int16_t> pcm;   // interleaved stereo, cut at the loop end for looping tracks
	int   rate = 44100;
	int   frames = 0;
	int   loopFrom = -1;        // frame to jump back to, or -1: play once
};
typedef std::shared_ptr<Track> TrackPtr;

struct Voice {
	TrackPtr t;
	double pos = 0;             // source frames
	double gain = 1.0;
	double fade = 0.0;          // per output frame, 0 = steady
};

struct MapEntry { std::set<int> stages; std::string track; };
struct LoopPts  { double s, e; };

std::mutex                       g_mixMu;      // protects voices (audio thread vs emulation thread)
Voice                            g_cur, g_old;
bool                             g_curOn = false, g_oldOn = false;
double                           g_volume = 0.8;
int                              g_outRate = 44100;

std::mutex                       g_loadMu;     // serializes track loading
std::map<std::string, TrackPtr>  g_tracks;
std::map<std::string, std::string> g_files;    // track number -> wav path
std::map<int, std::vector<MapEntry>> g_map;
std::map<std::string, LoopPts>   g_loops;
std::thread                      g_preload;
std::atomic<bool>                g_cancel(false);
std::atomic<bool>                g_active(false);
bool                             g_verbose = false;
bool                             g_hooked = false;
bool                             g_rush = false;
std::string                      g_dir;

const int MUSIC_IDS[] = { 0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,
                          0x10,0x11,0x12,0x33,0x35,0x36,0x37,0x38 };
// Same list the Lua hook used to decide when a request counts as "music" for the Boss Rush detector.
bool isMusicIdForRush(int id) {
	return (id >= 0x00 && id <= 0x12) || id == 0x33 || id == 0x35 || id == 0x36 || id == 0x38;
}
bool isMusicId(int id) {
	for (size_t i = 0; i < sizeof(MUSIC_IDS) / sizeof(MUSIC_IDS[0]); i++) if (MUSIC_IDS[i] == id) return true;
	return false;
}

// ---------------------------------------------------------------- small text helpers

std::vector<std::string> splitLines(const std::string &t) {
	std::vector<std::string> out;
	size_t i = 0;
	while (i <= t.size()) {
		size_t j = t.find('\n', i);
		if (j == std::string::npos) j = t.size();
		std::string l = t.substr(i, j - i);
		while (!l.empty() && (l.back() == '\r' || l.back() == ' ' || l.back() == '\t')) l.pop_back();
		size_t k = 0;
		while (k < l.size() && (l[k] == ' ' || l[k] == '\t')) k++;
		l = l.substr(k);
		if (!l.empty() && l[0] != '#') out.push_back(l);
		i = j + 1;
	}
	return out;
}

std::vector<std::string> splitChar(const std::string &s, char c) {
	std::vector<std::string> out;
	size_t i = 0;
	for (;;) {
		size_t j = s.find(c, i);
		if (j == std::string::npos) { out.push_back(s.substr(i)); break; }
		out.push_back(s.substr(i, j - i));
		i = j + 1;
	}
	for (size_t k = 0; k < out.size(); k++) {
		std::string &x = out[k];
		size_t a = 0, b = x.size();
		while (a < b && (x[a] == ' ' || x[a] == '\t')) a++;
		while (b > a && (x[b - 1] == ' ' || x[b - 1] == '\t')) b--;
		x = x.substr(a, b - a);
	}
	return out;
}

bool readTextFile(const std::string &path, std::string &out) {
	FILE *f = fopen(path.c_str(), "rb");
	if (!f) return false;
	char buf[4096]; size_t n;
	out.clear();
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
	fclose(f);
	return true;
}

void loadMap(const std::string &text) {
	g_map.clear();
	std::vector<std::string> lines = splitLines(text);
	for (size_t i = 0; i < lines.size(); i++) {
		std::vector<std::string> p = splitChar(lines[i], ',');
		if (p.size() < 3) continue;
		char *end = 0;
		long id = strtol(p[0].c_str(), &end, 16);
		if (end == p[0].c_str()) continue;
		MapEntry e;
		e.track = p[2];
		if (p[1] != "*") {
			std::vector<std::string> st = splitChar(p[1], ';');
			for (size_t k = 0; k < st.size(); k++) {
				char *e2 = 0;
				long v = strtol(st[k].c_str(), &e2, 16);
				if (e2 != st[k].c_str()) e.stages.insert((int)v);
			}
		}
		g_map[(int)id].push_back(e);
	}
}

void loadLoops(const std::string &text) {
	g_loops.clear();
	std::vector<std::string> lines = splitLines(text);
	for (size_t i = 0; i < lines.size(); i++) {
		std::vector<std::string> p = splitChar(lines[i], ',');
		if (p.size() < 3) continue;
		char *e1 = 0, *e2 = 0;
		double s = strtod(p[1].c_str(), &e1), e = strtod(p[2].c_str(), &e2);
		if (e1 != p[1].c_str() && e2 != p[2].c_str() && e > s) { LoopPts lp = { s, e }; g_loops[p[0]] = lp; }
	}
}

// ---------------------------------------------------------------- WAV reading

uint32_t rd32(const uint8_t *b) { return b[0] | (b[1] << 8) | (b[2] << 16) | ((uint32_t)b[3] << 24); }
uint16_t rd16(const uint8_t *b) { return (uint16_t)(b[0] | (b[1] << 8)); }

// PCM 8/16/24/32-bit or 32-bit float, mono or stereo, any sample rate. Output: interleaved stereo int16.
bool readWAV(const std::string &path, std::vector<int16_t> &out, int &rate) {
	FILE *f = fopen(path.c_str(), "rb");
	if (!f) return false;
	uint8_t hdr[12];
	if (fread(hdr, 1, 12, f) != 12 || memcmp(hdr, "RIFF", 4) || memcmp(hdr + 8, "WAVE", 4)) { fclose(f); return false; }
	int fmtTag = 0, ch = 0, bits = 0; rate = 0;
	bool haveFmt = false;
	for (;;) {
		uint8_t c8[8];
		if (fread(c8, 1, 8, f) != 8) { fclose(f); return false; }
		uint32_t size = rd32(c8 + 4);
		if (!memcmp(c8, "fmt ", 4)) {
			std::vector<uint8_t> b(size < 16 ? 16 : size);
			if (fread(b.data(), 1, size, f) != size) { fclose(f); return false; }
			fmtTag = rd16(&b[0]); ch = rd16(&b[2]); rate = (int)rd32(&b[4]); bits = rd16(&b[14]);
			if (fmtTag == 0xFFFE && size >= 26) fmtTag = rd16(&b[24]);   // WAVE_FORMAT_EXTENSIBLE
			haveFmt = true;
			if (size & 1) fseek(f, 1, SEEK_CUR);
		} else if (!memcmp(c8, "data", 4)) {
			if (!haveFmt || (ch != 1 && ch != 2) || rate < 8000) { fclose(f); return false; }
			bool isFloat = (fmtTag == 3 && bits == 32);
			if (!isFloat && !(fmtTag == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32))) { fclose(f); return false; }
			int bps = bits / 8;
			std::vector<uint8_t> raw;
			// a streamed WAV can carry 0 or 0xFFFFFFFF as size; read to the end of the file then
			if (size == 0 || size == 0xFFFFFFFFu) {
				uint8_t tmp[65536]; size_t n;
				while ((n = fread(tmp, 1, sizeof(tmp), f)) > 0) raw.insert(raw.end(), tmp, tmp + n);
			} else {
				raw.resize(size);
				size_t n = fread(raw.data(), 1, size, f);
				raw.resize(n);
			}
			fclose(f);
			size_t frames = raw.size() / (size_t)(bps * ch);
			out.resize(frames * 2);
			for (size_t i = 0; i < frames; i++) {
				for (int c = 0; c < 2; c++) {
					const uint8_t *p = &raw[(i * ch + (c < ch ? c : 0)) * bps];
					int v;
					if (isFloat) {
						float x; memcpy(&x, p, 4);
						int t = (int)lrintf(x * 32767.0f);
						v = t > 32767 ? 32767 : (t < -32768 ? -32768 : t);
					}
					else if (bits == 16) v = (int16_t)rd16(p);
					else if (bits == 8)  v = ((int)p[0] - 128) << 8;
					else if (bits == 24) v = (int16_t)rd16(p + 1);
					else                 v = (int16_t)rd16(p + 2);
					out[i * 2 + c] = (int16_t)v;
				}
			}
			return true;
		} else {
			fseek(f, (long)size + (size & 1), SEEK_CUR);
		}
	}
}

// ---------------------------------------------------------------- locating the album

// Accepts "NN.wav", "NN something.wav" and "... - NN Title.wav".
bool trackNumberFromName(const std::string &fn, std::string &num) {
	std::string lower = fn;
	for (size_t i = 0; i < lower.size(); i++) lower[i] = (char)tolower((unsigned char)lower[i]);
	if (lower.size() < 5 || lower.substr(lower.size() - 4) != ".wav") return false;
	std::string base = fn.substr(0, fn.size() - 4);
	size_t d = base.rfind(" - ");
	while (d != std::string::npos) {
		if (d + 5 <= base.size() && isdigit((unsigned char)base[d + 3]) && isdigit((unsigned char)base[d + 4]) &&
			(d + 5 == base.size() || !isdigit((unsigned char)base[d + 5]))) { num = base.substr(d + 3, 2); return true; }
		if (d == 0) break;
		d = base.rfind(" - ", d - 1);
	}
	if (base.size() >= 2 && isdigit((unsigned char)base[0]) && isdigit((unsigned char)base[1]) &&
		(base.size() == 2 || !isdigit((unsigned char)base[2]))) { num = base.substr(0, 2); return true; }
	return false;
}

void scanAlbum(const std::string &dir) {
	g_files.clear();
#ifdef WIN32
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA((dir + "*").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return;
	do {
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
		std::string num;
		if (trackNumberFromName(fd.cFileName, num) && !g_files.count(num)) g_files[num] = dir + fd.cFileName;
	} while (FindNextFileA(h, &fd));
	FindClose(h);
#else
	DIR *d = opendir(dir.c_str());
	if (!d) return;
	struct dirent *e;
	while ((e = readdir(d)) != NULL) {
		std::string num;
		if (trackNumberFromName(e->d_name, num) && !g_files.count(num)) g_files[num] = dir + e->d_name;
	}
	closedir(d);
#endif
}

TrackPtr loadTrack(const std::string &num) {
	std::lock_guard<std::mutex> lk(g_loadMu);
	std::map<std::string, TrackPtr>::iterator it = g_tracks.find(num);
	if (it != g_tracks.end()) return it->second;
	std::map<std::string, std::string>::iterator fi = g_files.find(num);
	if (fi == g_files.end()) return TrackPtr();
	TrackPtr t(new Track());
	t->num = num;
	if (!readWAV(fi->second, t->pcm, t->rate)) {
		FCEU_printf("MM3 audio: cannot read %s (need PCM WAV)\n", fi->second.c_str());
		g_tracks[num] = TrackPtr();
		return TrackPtr();
	}
	t->frames = (int)(t->pcm.size() / 2);
	std::map<std::string, LoopPts>::iterator li = g_loops.find(num);
	if (li != g_loops.end()) {
		const double xfadeSec = 0.20;
		int S = (int)(li->second.s * t->rate + 0.5), E = (int)(li->second.e * t->rate + 0.5);
		int xf = (int)(xfadeSec * t->rate);
		if (S >= xf && E <= t->frames && E - S >= xf * 2) {
			// Blend the last xf frames before E toward the audio that precedes S so that
			// jumping from E back to S continues smoothly.
			for (int i = 0; i < xf; i++) {
				double w = (double)i / xf;
				for (int c = 0; c < 2; c++) {
					double a = t->pcm[2 * (E - xf + i) + c], b = t->pcm[2 * (S - xf + i) + c];
					t->pcm[2 * (E - xf + i) + c] = (int16_t)(a * (1 - w) + b * w);
				}
			}
			t->pcm.resize((size_t)E * 2);
			t->pcm.shrink_to_fit();
			t->frames = E;
			t->loopFrom = S;
		}
	}
	g_tracks[num] = t;
	return t;
}

// ---------------------------------------------------------------- playback control

void fadeCurrentLocked() {
	if (g_curOn) {
		g_cur.fade = 1.0 / (0.08 * g_outRate);
		g_old = g_cur;
		g_oldOn = true;
		g_curOn = false;
		g_cur.t.reset();
	}
}

void playTrack(const std::string &num) {
	TrackPtr t = loadTrack(num);
	if (!t) return;
	std::lock_guard<std::mutex> lk(g_mixMu);
	fadeCurrentLocked();
	g_cur = Voice();
	g_cur.t = t; g_cur.gain = 1.0; g_cur.pos = 0;
	g_curOn = true;
}

void stopPlayback() {
	std::lock_guard<std::mutex> lk(g_mixMu);
	fadeCurrentLocked();
}

// A sound request (id) with the stage number ($22), as the companion program saw it.
void handleRequest(int id, int stage) {
	if (id == 0xF0 || id == 0xF2) { stopPlayback(); return; }
	std::map<int, std::vector<MapEntry>>::iterator it = g_map.find(id);
	if (it != g_map.end()) {
		for (size_t i = 0; i < it->second.size(); i++) {
			const MapEntry &e = it->second[i];
			if (e.stages.empty() || e.stages.count(stage)) {
				if (g_files.count(e.track)) {
					if (g_verbose) FCEU_printf("MM3 audio: sound %02X (stage %02X) -> track %s\n", id, stage, e.track.c_str());
					playTrack(e.track);
					return;
				}
			}
		}
	}
	if (isMusicId(id)) {
		if (g_verbose) FCEU_printf("MM3 audio: sound %02X (stage %02X) unmapped music, stopping album\n", id, stage);
		stopPlayback();
	}
}

// ---------------------------------------------------------------- CPU hook (replaces mm3_sound_hook.lua)

// The three places where the game calls its sound driver ("JSR $8003") in the fixed banks.
const unsigned ADDR_POP = 0xFFBC, ADDR_NMI = 0xC107, ADDR_CLR = 0xCBFD;

void execHook(unsigned address, unsigned value, void *userData) {
	(void)value; (void)userData;
	if (address != ADDR_POP && address != ADDR_NMI && address != ADDR_CLR) return;
	int id = X.A;
	int stage = RAM[0x22], scr = RAM[0xF9];
	// $CBFD is a stage-clear cutscene call, not a music request.
	if (address == ADDR_CLR) return;

	handleRequest(id, stage);

	// Wily 4 Boss Rush (the eight Robot Master refights) sends no music request of its own, so two extra
	// events are derived here: A0 = first refight boss started, A1 = eighth refight boss defeated.
	if (isMusicIdForRush(id)) {
		g_rush = false;
	} else if (stage == 0x0F) {
		if (id == 0x1C && !g_rush && scr >= 0x0A && scr <= 0x18) {
			g_rush = true;
			handleRequest(0xA0, stage);
		} else if (id == 0x17 && g_rush && scr == 0x0A) {
			g_rush = false;
			handleRequest(0xA1, stage);
		}
	}
}

void removeHook() {
	if (g_hooked) {
		X6502_MemHook::Remove(X6502_MemHook::Exec, execHook, nullptr);
		g_hooked = false;
	}
}

bool looksLikeMM3() {
	if (!GameInfo || GameInfo->type != GIT_CART || GameInfo->mappernum != 4) return false;
	const unsigned sites[3] = { ADDR_POP, ADDR_NMI, ADDR_CLR };
	for (int i = 0; i < 3; i++) {
		unsigned a = sites[i];
		if (ARead[a](a) != 0x20 || ARead[a + 1](a + 1) != 0x03 || ARead[a + 2](a + 2) != 0x80) return false;
	}
	return true;
}

void stopPreload() {
	g_cancel = true;
	if (g_preload.joinable()) g_preload.join();
	g_cancel = false;
}

} // namespace

// ================================================================ public API

void MM3Audio_GameLoaded(void) {
	MM3Audio_GameClosed();
	if (!looksLikeMM3()) return;

	const char *base = FCEUI_GetBaseDirectory();
	g_dir = std::string(base ? base : "") + "/mm3_remade/";

	// settings
	g_volume = 0.8; g_verbose = false;
	bool enabled = true, preload = true;
	std::string cfg;
	if (readTextFile(g_dir + "config.txt", cfg)) {
		std::vector<std::string> lines = splitLines(cfg);
		for (size_t i = 0; i < lines.size(); i++) {
			size_t eq = lines[i].find('=');
			if (eq == std::string::npos) continue;
			std::string k = lines[i].substr(0, eq), v = lines[i].substr(eq + 1);
			std::vector<std::string> kk = splitChar(k, ','), vv = splitChar(v, ',');
			k = kk[0]; v = vv[0];
			if (k == "volume") g_volume = std::max(0.0, std::min(1.0, atof(v.c_str())));
			else if (k == "enabled") enabled = atoi(v.c_str()) != 0;
			else if (k == "preload") preload = atoi(v.c_str()) != 0;
			else if (k == "verbose") g_verbose = atoi(v.c_str()) != 0;
		}
	}
	if (!enabled) return;

	scanAlbum(g_dir);
	if (g_files.empty()) {
		FCEU_printf("MM3 audio: Mega Man 3 detected, but no WAV tracks in %s (put 01.wav ... 31.wav there)\n", g_dir.c_str());
		return;
	}
	std::string t;
	loadMap(readTextFile(g_dir + "map.csv", t) ? t : std::string(MM3_DEFAULT_MAP));
	loadLoops(readTextFile(g_dir + "loops.csv", t) ? t : std::string(MM3_DEFAULT_LOOPS));

	g_rush = false;
	X6502_MemHook::Add(X6502_MemHook::Exec, execHook, nullptr);
	g_hooked = true;
	g_active = true;
	FCEU_printf("MM3 audio: %d album tracks found in %s\n", (int)g_files.size(), g_dir.c_str());

	if (preload) {
		std::vector<std::string> nums;
		std::set<std::string> seen;
		for (std::map<int, std::vector<MapEntry>>::iterator it = g_map.begin(); it != g_map.end(); ++it)
			for (size_t i = 0; i < it->second.size(); i++)
				if (g_files.count(it->second[i].track) && seen.insert(it->second[i].track).second) nums.push_back(it->second[i].track);
		g_preload = std::thread([nums]() {
			for (size_t i = 0; i < nums.size() && !g_cancel; i++) loadTrack(nums[i]);
		});
	}
}

void MM3Audio_GameClosed(void) {
	stopPreload();
	removeHook();
	g_active = false;
	{
		std::lock_guard<std::mutex> lk(g_mixMu);
		g_curOn = g_oldOn = false;
		g_cur = Voice(); g_old = Voice();
	}
	std::lock_guard<std::mutex> lk(g_loadMu);
	g_tracks.clear();
	g_files.clear();
	g_map.clear();
	g_loops.clear();
	g_rush = false;
}

void MM3Audio_Stop(void) {
	if (!g_active) return;
	g_rush = false;
	stopPlayback();
}

int MM3Audio_Active(void) { return g_active ? 1 : 0; }

static bool mixVoice(Voice &v, int16_t *out, int frames, int outRate, bool ownFade) {
	const Track &t = *v.t;
	const double step = (double)t.rate / outRate;
	const int16_t *p = t.pcm.data();
	const double vol = g_volume;
	for (int i = 0; i < frames; i++) {
		if (v.fade > 0) {
			v.gain -= v.fade;
			if (v.gain <= 0) return false;
		}
		int idx = (int)v.pos;
		if (idx >= t.frames) {
			if (t.loopFrom < 0) return false;
			v.pos = t.loopFrom + (v.pos - t.frames);
			idx = (int)v.pos;
			if (idx >= t.frames) idx = t.loopFrom;
		}
		double fr = v.pos - idx;
		int idx2 = idx + 1;
		if (idx2 >= t.frames) idx2 = (t.loopFrom >= 0) ? t.loopFrom : idx;
		double g = v.gain * vol;
		for (int c = 0; c < 2; c++) {
			double s0 = p[2 * idx + c], s1 = p[2 * idx2 + c];
			double s = (s0 + (s1 - s0) * fr) * g;
			double o = out[2 * i + c] + s;
			if (o > 32767) o = 32767; else if (o < -32768) o = -32768;
			out[2 * i + c] = (int16_t)lrint(o);
		}
		v.pos += step;
	}
	(void)ownFade;
	return true;
}

void MM3Audio_Mix(int16_t *stereo, int frames, int outRate, int silent) {
	if (!g_active || silent || outRate <= 0 || frames <= 0) return;
	std::lock_guard<std::mutex> lk(g_mixMu);
	g_outRate = outRate;
	if (g_oldOn && !mixVoice(g_old, stereo, frames, outRate, true)) { g_oldOn = false; g_old = Voice(); }
	if (g_curOn && !mixVoice(g_cur, stereo, frames, outRate, false)) { g_curOn = false; g_cur = Voice(); }
}
