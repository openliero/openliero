#include "mixer.hpp"
#include <SDL_atomic.h>
#include <stdlib.h>
#include <string.h>
#include <cstring>
#include <vector>

typedef struct channel {
  // Mutator write once, mutator read, mixer write
  void* id;

  // Mutator write, mixer read
  uint32_t flags;

  // Mutator write once, mixer read
  sfx_sound* sound;

  // Mutator write once, mixer read/write
  uint32_t pos;

  uint64_t soundpos, stride;
  uint32_t volumes;

  /*
  Mutator can change flags from 0xFFFFFFFF, signaling a new active channel
  Mixer or mutator can change flags to 0xFFFFFFFF, signaling a new inactive
  channel
  */
} channel;

struct sfx_mixer {
  channel channel_states[CHANNEL_COUNT];

  int32_t base_frame;
  int initialized;
  double output_latency;
  double sample_rate;
};

struct sfx_sound {
  std::vector<int16_t> samples;
};

int32_t sfx_mixer_now(const sfx_mixer* mixer) {
  return mixer->base_frame;
}

sfx_sound* sfx_new_sound(std::size_t samples) {
  sfx_sound* snd = new sfx_sound();

  snd->samples = std::vector<int16_t>(samples);
  return snd;
}

void sfx_free_sound(sfx_sound* snd) {
  delete snd;
}

std::vector<int16_t>& sfx_sound_data(sfx_sound* snd) {
  return snd->samples;
}

#define INACTIVE_FLAGS (-1)
#define MK_HANDLE(num, idx) static_cast<uint32>(((num) << 8) + (idx))
#define CHECK_HANDLE(h) \
  (((self)->channel_states[(h) & 0xff].id == (h)) ? ((h) & 0xff) : -1)

sfx_mixer* sfx_mixer_create(void) {
  sfx_mixer* self = new sfx_mixer();
  uint32_t i;
  for (i = 0; i < CHANNEL_COUNT; ++i)
    self->channel_states[i].flags = INACTIVE_FLAGS;

  return self;
}

static int add_channel(
    int16_t* out,
    unsigned long frame_count,
    uint32_t now,
    channel* ch) {
  sfx_sound* sound = ch->sound;
  uint32_t pos = ch->pos;
  int32_t scaler;
  uint32_t cur;

  // TODO: Get rid of undefined cast
  int32_t diff = static_cast<int32_t>(pos - now);
  int32_t relbegin = diff;
  uint32_t soundlen = sound->samples.size();
  uint32_t flags = ch->flags;

  if (relbegin < 0)
    relbegin = 0;
  if (flags == INACTIVE_FLAGS)
    return 0;  // Stop

  scaler = (ch->volumes & 0x1fff);

  for (cur = static_cast<uint32_t>(relbegin); cur < frame_count; ++cur) {
    int32_t samp = out[cur];
    int32_t soundsamp =
        sound->samples[static_cast<uint32_t>(ch->soundpos >> 32)];
    samp += (soundsamp * scaler) >> 12;
    if (samp < -32768)
      samp = -32768;
    else if (samp > 32767)
      samp = 32767;
    out[cur] = static_cast<int32_t>(samp);
    ch->soundpos += ch->stride;
    if (static_cast<uint32_t>(ch->soundpos >> 32) >= soundlen) {
      if (flags & SFX_SOUND_LOOP)
        ch->soundpos = 0;
      else
        return 0;
    }
  }

  return 1;
}

void sfx_mixer_mix(sfx_mixer* self, void* output, unsigned long frame_count) {
  int16_t* out = static_cast<int16_t*>(output);
  uint32_t c;
  memset(out, 0, frame_count * sizeof(int16_t));

  // TODO: Handle discontinuous mixing.
  // i.e. if (now + frame_count) in one call differs from (now) in the next,
  // we need to advance soundpos for channels to be accurate.

  for (c = 0; c < CHANNEL_COUNT; ++c) {
    channel* ch = self->channel_states + c;

    if (ch->flags != INACTIVE_FLAGS) {
      if (!add_channel(out, frame_count, self->base_frame, ch)) {
        // Remove
        SDL_MemoryBarrierAcquire();
        ch->flags = INACTIVE_FLAGS;
        ch->id = NULL;
        SDL_MemoryBarrierRelease();  // Make ch->flags and ch->id write visible
      }
    }
  }

  self->base_frame += frame_count;
}

/*
void sfx_mixer_fill(sfx_stream* str, uint32_t start, uint32_t frames)
{
        sfx_mixer* self = (sfx_mixer*)str->ud;
        sfx_mixer_mix(self, str->buffer, frames, start);
}*/

static int find_channel(const sfx_mixer* self, const void* h) {
  uint32_t c;

  for (c = 0; c < CHANNEL_COUNT;) {
    if (self->channel_states[c].id == h)
      return static_cast<int>(c);
    ++c;
  }

  return -1;
}

static int find_free_channel(const sfx_mixer* self) {
  uint32_t c;

  for (c = 0; c < CHANNEL_COUNT;) {
    if (self->channel_states[c].flags == INACTIVE_FLAGS)
      return static_cast<int>(c);
    ++c;
  }

  return -1;
}

void sfx_set_volume(sfx_mixer* self, const void* h, double volume) {
  uint32_t v;
  int ch = find_channel(self, h);
  if (ch < 0)
    return;

  v = static_cast<uint32_t>(volume * 0x1000);

  self->channel_states[ch].volumes = (v << 16) + v;
}

int sfx_is_playing(const sfx_mixer* self, const void* h) {
  int ch = find_channel(self, h);
  return (ch >= 0);
}

void sfx_mixer_stop(sfx_mixer* self, const void* h) {
  int ch = find_channel(self, h);
  if (ch < 0)
    return;

  self->channel_states[ch].flags = -1;
}

void* sfx_mixer_add(
    sfx_mixer* self,
    sfx_sound* snd,
    uint32_t time,
    void* h,
    uint32_t flags) {
  int ch_idx = find_free_channel(self);

  if (ch_idx >= 0) {
    SDL_MemoryBarrierAcquire();
    channel* ch = self->channel_states + ch_idx;
    ch->sound = snd;
    ch->pos = time;
    ch->soundpos = 0;
    ch->stride = 0x100000000ull;
    ch->volumes = 0x10001000;
    ch->id = h;

    SDL_MemoryBarrierRelease();  // Make sure everything is written before
                                 // ch->flags

    ch->flags = flags;
    return h;
  }

  return NULL;
}

#undef CHECK_HANDLE
