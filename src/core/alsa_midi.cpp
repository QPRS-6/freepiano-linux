// ALSA sequencer MIDI I/O.  Replaces WinMM midiIn/midiOut from the Windows
// build, and gives the same "MIDI keyboard in / MIDI synth out" features.

#include "alsa_midi.h"

#include "sync.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#ifdef FP_HAVE_ALSA

#include <alsa/asoundlib.h>
#include <poll.h>

namespace {

snd_seq_t* g_seq = nullptr;
int g_out_port = -1;
int g_in_port = -1;
int g_connected_client = -1;
int g_connected_port = -1;
int g_input_client = -1;
int g_input_port = -1;

std::thread g_input_thread;
std::atomic<bool> g_input_running{false};
alsa_midi_input_callback_t g_input_callback;

std::mutex g_seq_mutex;

bool ensure_seq() {
  if (g_seq) return true;
  if (snd_seq_open(&g_seq, "default", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK) < 0) {
    g_seq = nullptr;
    return false;
  }
  snd_seq_set_client_name(g_seq, "FreePiano");
  return true;
}

std::string port_label(int client, int port) {
  char client_name[64] = {};
  char port_name[128] = {};
  snd_seq_client_info_t* cinfo;
  snd_seq_port_info_t* pinfo;
  snd_seq_client_info_alloca(&cinfo);
  snd_seq_port_info_alloca(&pinfo);

  std::string label;
  if (snd_seq_get_any_client_info(g_seq, client, cinfo) == 0) {
    std::snprintf(client_name, sizeof(client_name), "%s",
                  snd_seq_client_info_get_name(cinfo) ? snd_seq_client_info_get_name(cinfo) : "?");
  } else {
    std::snprintf(client_name, sizeof(client_name), "client %d", client);
  }
  if (snd_seq_get_any_port_info(g_seq, client, port, pinfo) == 0) {
    std::snprintf(port_name, sizeof(port_name), "%s",
                  snd_seq_port_info_get_name(pinfo) ? snd_seq_port_info_get_name(pinfo) : "?");
  }
  char id[32];
  std::snprintf(id, sizeof(id), "%d:%d", client, port);
  label = std::string(id) + "  " + client_name;
  if (port_name[0] && std::strcmp(port_name, client_name) != 0) label += std::string(" / ") + port_name;
  return label;
}

std::vector<std::string> enum_ports(bool want_input) {
  std::vector<std::string> result;
  if (!ensure_seq()) return result;

  snd_seq_client_info_t* cinfo;
  snd_seq_port_info_t* pinfo;
  snd_seq_client_info_alloca(&cinfo);
  snd_seq_port_info_alloca(&pinfo);

  int self = snd_seq_client_id(g_seq);
  snd_seq_client_info_set_client(cinfo, -1);
  while (snd_seq_query_next_client(g_seq, cinfo) >= 0) {
    int client = snd_seq_client_info_get_client(cinfo);
    if (client == self) continue;
    snd_seq_port_info_set_client(pinfo, client);
    snd_seq_port_info_set_port(pinfo, -1);
    while (snd_seq_query_next_port(g_seq, pinfo) >= 0) {
      unsigned caps = snd_seq_port_info_get_capability(pinfo);
      unsigned type = snd_seq_port_info_get_type(pinfo);
      if (!(type & SND_SEQ_PORT_TYPE_MIDI_GENERIC)) continue;
      bool good = want_input ? (caps & (SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ)) ==
                                   (SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ)
                             : (caps & (SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE)) ==
                                   (SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE);
      if (!good) continue;
      result.push_back(port_label(client, snd_seq_port_info_get_port(pinfo)));
    }
  }
  return result;
}

bool parse_port_id(const std::string& label, int* client, int* port) {
  int c = 0, p = 0;
  if (std::sscanf(label.c_str(), "%d:%d", &c, &p) != 2) return false;
  *client = c;
  *port = p;
  return true;
}

void dispatch_midi(byte a, byte b, byte c, byte d) {
  if (g_input_callback) g_input_callback(a, b, c, d);
}

void handle_event(const snd_seq_event_t* ev) {
  byte ch = (byte)(ev->data.note.channel & 0x0f);
  switch (ev->type) {
    case SND_SEQ_EVENT_NOTEON:
      dispatch_midi((byte)(0x90 | ch), (byte)ev->data.note.note, (byte)ev->data.note.velocity, 0);
      break;
    case SND_SEQ_EVENT_NOTEOFF:
      dispatch_midi((byte)(0x80 | ch), (byte)ev->data.note.note, (byte)ev->data.note.velocity, 0);
      break;
    case SND_SEQ_EVENT_KEYPRESS:
      dispatch_midi((byte)(0xa0 | ch), (byte)ev->data.note.note, (byte)ev->data.note.velocity, 0);
      break;
    case SND_SEQ_EVENT_CONTROLLER:
      dispatch_midi((byte)(0xb0 | ch), (byte)ev->data.control.param, (byte)ev->data.control.value, 0);
      break;
    case SND_SEQ_EVENT_PGMCHANGE:
      dispatch_midi((byte)(0xc0 | ch), (byte)ev->data.control.value, 0, 0);
      break;
    case SND_SEQ_EVENT_CHANPRESS:
      dispatch_midi((byte)(0xd0 | ch), (byte)ev->data.control.value, 0, 0);
      break;
    case SND_SEQ_EVENT_PITCHBEND: {
      int bend = ev->data.control.value + 8192;
      if (bend < 0) bend = 0;
      if (bend > 16383) bend = 16383;
      dispatch_midi((byte)(0xe0 | ch), (byte)(bend & 0x7f), (byte)((bend >> 7) & 0x7f), 0);
      break;
    }
    default:
      break;
  }
}

void input_loop() {
  snd_seq_t* seq = g_seq;
  if (!seq) return;

  struct pollfd pfds[8];
  int nfds = snd_seq_poll_descriptors(seq, pfds, 8, POLLIN);
  while (g_input_running) {
    int ret = poll(pfds, nfds, 100);
    if (ret <= 0) continue;
    snd_seq_event_t* ev = nullptr;
    while (snd_seq_event_input(seq, &ev) >= 0 && ev) {
      handle_event(ev);
    }
  }
}

}  // namespace

bool alsa_midi_available() { return ensure_seq(); }

std::vector<std::string> alsa_midi_output_ports() { return enum_ports(false); }
std::vector<std::string> alsa_midi_input_ports() { return enum_ports(true); }

bool alsa_midi_open_output(const std::string& port) {
  fp_lock lock;
  if (!ensure_seq()) return false;
  alsa_midi_close_output();

  if (g_out_port < 0) {
    g_out_port = snd_seq_create_simple_port(
        g_seq, "FreePiano Out",
        SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
        SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    if (g_out_port < 0) return false;
  }

  int client = 0, p = 0;
  if (!parse_port_id(port, &client, &p)) return false;
  if (snd_seq_connect_to(g_seq, g_out_port, client, p) < 0) return false;
  g_connected_client = client;
  g_connected_port = p;
  return true;
}

void alsa_midi_close_output() {
  fp_lock lock;
  if (g_seq && g_out_port >= 0 && g_connected_client >= 0) {
    snd_seq_disconnect_to(g_seq, g_out_port, g_connected_client, g_connected_port);
  }
  g_connected_client = -1;
  g_connected_port = -1;
}

bool alsa_midi_output_open() { return g_connected_client >= 0; }

void alsa_midi_send(byte a, byte b, byte c, byte d) {
  if (!g_seq || g_out_port < 0 || g_connected_client < 0) return;

  snd_seq_event_t ev;
  snd_seq_ev_clear(&ev);
  snd_seq_ev_set_source(&ev, g_out_port);
  snd_seq_ev_set_dest(&ev, g_connected_client, g_connected_port);

  byte type = a & 0xf0;
  byte ch = a & 0x0f;
  switch (type) {
    case 0x80:
      snd_seq_ev_set_noteoff(&ev, ch, b, c);
      break;
    case 0x90:
      snd_seq_ev_set_noteon(&ev, ch, b, c);
      break;
    case 0xa0:
      snd_seq_ev_set_keypress(&ev, ch, b, c);
      break;
    case 0xb0:
      snd_seq_ev_set_controller(&ev, ch, b, c);
      break;
    case 0xc0:
      snd_seq_ev_set_pgmchange(&ev, ch, b);
      break;
    case 0xd0:
      snd_seq_ev_set_chanpress(&ev, ch, b);
      break;
    case 0xe0: {
      int bend = ((c & 0x7f) << 7 | (b & 0x7f)) - 8192;
      snd_seq_ev_set_pitchbend(&ev, ch, bend);
      break;
    }
    default:
      return;
  }
  snd_seq_ev_set_direct(&ev);
  snd_seq_event_output_direct(g_seq, &ev);
}

bool alsa_midi_open_input(const std::string& port, alsa_midi_input_callback_t callback) {
  fp_lock lock;
  if (!ensure_seq()) return false;
  alsa_midi_close_input();

  if (g_in_port < 0) {
    g_in_port = snd_seq_create_simple_port(
        g_seq, "FreePiano In",
        SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
        SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    if (g_in_port < 0) return false;
  }

  int client = 0, p = 0;
  if (!parse_port_id(port, &client, &p)) return false;
  if (snd_seq_connect_from(g_seq, g_in_port, client, p) < 0) return false;

  g_input_client = client;
  g_input_port = p;
  g_input_callback = std::move(callback);
  g_input_running = true;
  g_input_thread = std::thread(input_loop);
  return true;
}

void alsa_midi_close_input() {
  fp_lock lock;
  g_input_running = false;
  if (g_input_thread.joinable()) g_input_thread.join();
  if (g_seq && g_in_port >= 0 && g_input_client >= 0) {
    snd_seq_disconnect_from(g_seq, g_in_port, g_input_client, g_input_port);
  }
  g_input_client = -1;
  g_input_port = -1;
  g_input_callback = nullptr;
}

bool alsa_midi_input_open() { return g_input_client >= 0; }

void alsa_midi_shutdown() {
  alsa_midi_close_input();
  alsa_midi_close_output();
  fp_lock lock;
  if (g_seq) {
    snd_seq_close(g_seq);
    g_seq = nullptr;
    g_out_port = -1;
    g_in_port = -1;
  }
}

#else  // !FP_HAVE_ALSA

bool alsa_midi_available() { return false; }
std::vector<std::string> alsa_midi_output_ports() { return {}; }
std::vector<std::string> alsa_midi_input_ports() { return {}; }
bool alsa_midi_open_output(const std::string&) { return false; }
void alsa_midi_close_output() {}
bool alsa_midi_output_open() { return false; }
void alsa_midi_send(byte, byte, byte, byte) {}
bool alsa_midi_open_input(const std::string&, alsa_midi_input_callback_t) { return false; }
void alsa_midi_close_input() {}
bool alsa_midi_input_open() { return false; }
void alsa_midi_shutdown() {}

#endif
