#include <stdio.h>
#include <unistd.h>
#include <SDL2/SDL.h>

#include "blip_buf.h"
#include "tfmx.h"

blip_buffer_t* bb[2];
int prevSample[2]={0,0};
short bbOut[2][32768];

SDL_AudioDeviceID ai;
SDL_AudioSpec ac;
SDL_AudioSpec ar;

bool quit, ntsc;

int sr;
double targetSR;

TFMXPlayer p;

static void process(void* userdata, Uint8* stream, int len) {
  short* buf[2];
  short temp[2];
  int wc;
  int writable;
  unsigned int nframes=len/(2*ar.channels);
  buf[0]=(short*)stream;
  buf[1]=&buf[0][1];
  
#ifdef HLE
  int runtotal=nframes;
#else
  int runtotal=blip_clocks_needed(bb[0],nframes);
#endif

  for (size_t i=0; i<runtotal; i++) {
    p.nextSample(&temp[0],&temp[1]);

#ifdef HLE
    buf[0][i*ar.channels]=temp[0]+(temp[1]>>2);
    buf[1][i*ar.channels]=temp[1]+(temp[0]>>2);
#else
    blip_add_delta(bb[0],i,(short)(temp[0]-prevSample[0]));
    blip_add_delta(bb[1],i,(short)(temp[1]-prevSample[1]));
    prevSample[0]=temp[0];
    prevSample[1]=temp[1];
#endif
  }

#ifndef HLE
  blip_end_frame(bb[0],runtotal);
  blip_end_frame(bb[1],runtotal);

  blip_read_samples(bb[0],bbOut[0],nframes,0);
  blip_read_samples(bb[1],bbOut[1],nframes,0);

  for (size_t i=0; i<nframes; i++) {
    buf[0][i*ar.channels]=bbOut[0][i]+(bbOut[1][i]>>2);
    buf[1][i*ar.channels]=bbOut[1][i]+(bbOut[0][i]>>2);
  }
#endif
}

int main(int argc, char** argv) {
  int songid;
  ntsc=false;
  if (argc<3) {
    printf("usage: %s mdat.file smpl.file [song]\n",argv[0]);
    return 1;
  }
  songid=0;
  if (argc>3) {
    songid=atoi(argv[3]);
  }
  if (!p.load(argv[1],argv[2])) {
    printf("could not open song...\n");
    return 1;
  }
  p.play(songid);
  printf("opening audio\n");
  
  SDL_Init(SDL_INIT_AUDIO);

  ac.freq=44100;
  ac.format=AUDIO_S16;
  ac.channels=2;
  ac.samples=1024;
  ac.callback=process;
  ac.userdata=NULL;
  ai=SDL_OpenAudioDevice(SDL_GetAudioDeviceName(0,0),0,&ac,&ar,SDL_AUDIO_ALLOW_ANY_CHANGE);
  sr=ar.freq;
  if (ntsc) {
    targetSR=3579545;
    p.setCIAVal(59659);
  } else {
    targetSR=3546895;
    p.setCIAVal(70937);
  }

  bb[0]=blip_new(32768);
  bb[1]=blip_new(32768);
  blip_set_rates(bb[0],targetSR,sr);
  blip_set_rates(bb[1],targetSR,sr);

  p.hleRate=float((double)targetSR/(double)sr);

  printf("running.\n");
  SDL_PauseAudioDevice(ai,0);
  
  while (!quit) {
    usleep(50000);
  }

  SDL_CloseAudioDevice(ai);
  return 0;
}
