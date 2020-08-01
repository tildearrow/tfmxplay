#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <string>
#include <vector>
#include <SDL2/SDL.h>
#ifdef _SYNC_VBLANK
#include <fcntl.h>
#include <libdrm/drm.h>
#endif

#include "../ta-time.h"

#include "blip_buf.h"
#include "tfmx.h"

typedef std::string string;

struct Param {
  string shortName;
  string name;
  string valName;
  string desc;
  bool value;
  bool (*func)(string);
  Param(string sn, string n, bool v, bool (*f)(string), string vn, string d): shortName(sn), name(n), valName(vn), desc(d), value(v), func(f) {}
};

std::vector<Param> params;

bool needsValue(string param) {
  for (size_t i=0; i<params.size(); i++) {
    if (params[i].name==param || params[i].shortName==param) {
      return params[i].value;
    }
  }
  return false;
}

blip_buffer_t* bb[2];
int prevSample[2]={0,0};
short bbOut[2][32768];

SDL_AudioDeviceID ai;
SDL_AudioSpec ac;
SDL_AudioSpec ar;

bool quit, ntsc, hle, dumpFile;
FILE* dump;

int sr, speed;
double targetSR;
int songid;

TFMXPlayer p;

struct sigaction intsa;
struct termios termprop;
struct termios termpropold;

#ifdef _SYNC_VBLANK
int syncfd;
bool syncVBlank;
#endif

const char* truth[]={
  "false", "true"
};

void finish() {
  if (tcsetattr(0,TCSAFLUSH,&termpropold)!=0) {
    printf("WARNING: FAILURE TO SET FLAGS TO QUIT!\n");
    return;
  }
}

static void handleTerm(int data) {
  quit=true;
  printf("quit!\n");
  SDL_CloseAudioDevice(ai);
  finish();
  if (dumpFile) {
    printf("closing dump\n");
    fflush(dump);
    fclose(dump);
    dumpFile=false;
  }
  exit(0);
}

static void processHLE(void* userdata, Uint8* stream, int len) {
  short* buf[2];
  short temp[2];
  int wc;
  int writable;
  unsigned int nframes=len/(2*ar.channels);
  buf[0]=(short*)stream;
  buf[1]=&buf[0][1];

  int runtotal=nframes;

  for (size_t i=0; i<runtotal; i++) {
    p.nextSampleHLE(&temp[0],&temp[1]);

    buf[0][i*ar.channels]=(temp[0]+(temp[1]>>2))<<1;
    buf[1][i*ar.channels]=(temp[1]+(temp[0]>>2))<<1;
    //buf[0][i*ar.channels]=temp[0];
    //buf[1][i*ar.channels]=temp[1];
  }
  if (dumpFile) {
    if (fwrite(stream,1,len,dump)<0) {
      perror("cannot write");
      printf("stopping dump!\n");
      fclose(dump);
      dumpFile=false;
    }
  }
}

static void process(void* userdata, Uint8* stream, int len) {
  short* buf[2];
  short temp[2];
  int wc;
  int writable;
  unsigned int nframes=len/(2*ar.channels);
  buf[0]=(short*)stream;
  buf[1]=&buf[0][1];
  
  blip_set_rates(bb[0],targetSR,sr);
  blip_set_rates(bb[1],targetSR,sr);
  p.hleRate=float((double)targetSR/(double)sr);
  
  int runtotal=blip_clocks_needed(bb[0],nframes);

  for (size_t i=0; i<runtotal; i++) {
    p.nextSample(&temp[0],&temp[1]);

    blip_add_delta(bb[0],i,(temp[0]+(temp[1]>>2)-prevSample[0])<<1);
    blip_add_delta(bb[1],i,(temp[1]+(temp[0]>>2)-prevSample[1])<<1);
    prevSample[0]=temp[0]+(temp[1]>>2);
    prevSample[1]=temp[1]+(temp[0]>>2);
  }

  blip_end_frame(bb[0],runtotal);
  blip_end_frame(bb[1],runtotal);

  blip_read_samples(bb[0],bbOut[0],nframes,0);
  blip_read_samples(bb[1],bbOut[1],nframes,0);

  for (size_t i=0; i<nframes; i++) {
    buf[0][i*ar.channels]=bbOut[0][i];
    buf[1][i*ar.channels]=bbOut[1][i];
  }

  if (dumpFile) {
    if (fwrite(stream,1,len,dump)<0) {
      perror("cannot write");
      printf("stopping dump!\n");
      fclose(dump);
      dumpFile=false;
    }
  }
}

bool parHelp(string) {
  printf("usage: tfmxplay [-params] mdat.file [smpl.file]\n");
  for (auto& i: params) {
    if (i.value) {
      printf("  -%s %s: %s\n",i.name.c_str(),i.valName.c_str(),i.desc.c_str());
    } else {
      printf("  -%s: %s\n",i.name.c_str(),i.desc.c_str());
    }
  }
  return false;
}

bool parNTSC(string) {
  ntsc=true;
  return true;
}

bool parSong(string v) {
  try {
    songid=std::stoi(v);
    if (songid<0 || songid>127) {
      printf("song number must be between 0 and 127.\n");
      return false;
    }
  } catch (std::exception& e) {
    printf("type a number, silly.\n");
    return false;
  }
  return true;
}

bool parHLE(string) {
  hle=true;
  return true;
}

#ifdef _SYNC_VBLANK
bool parVBlank(string) {
  syncVBlank=true;
  return true;
}
#endif

bool parDump(string) {
  dumpFile=true;
  return true;
}

void initParams() {
  params.push_back(Param("h","help",false,parHelp,"","display this help"));

  params.push_back(Param("s","song",true,parSong,"num","select song"));
  params.push_back(Param("n","ntsc",false,parNTSC,"","use NTSC rate"));
  params.push_back(Param("l","hle",false,parHLE,"","use high-level emulation (lower quality but much faster)"));
  params.push_back(Param("d","dump",false,parDump,"","dump 16-bit stereo raw output to tfmx.raw"));

#ifdef _SYNC_VBLANK
  params.push_back(Param("V","vblank",false,parVBlank,"","sync to VBlank"));
#endif
}

int main(int argc, char** argv) {
  string mdat, smpl;
  ntsc=false;
  dumpFile=false;
  songid=0;
#ifdef _SYNC_VBLANK
  syncVBlank=false;
#endif

  initParams();

  // parse arguments
  string arg, val;
  size_t eqSplit, argStart;
  for (int i=1; i<argc; i++) {
    arg=""; val="";
    if (argv[i][0]=='-') {
      if (argv[i][1]=='-') {
        argStart=2;
      } else {
        argStart=1;
      }
      arg=&argv[i][argStart];
      eqSplit=arg.find_first_of('=');
      if (eqSplit==string::npos) {
        if (needsValue(arg)) {
          if ((i+1)<argc) {
            val=argv[i+1];
            i++;
          } else {
            printf("incomplete param %s.\n",arg.c_str());
            return 1;
          }
        }
      } else {
        val=arg.substr(eqSplit+1);
        arg=arg.substr(0,eqSplit);
      }
      //printf("arg %s. val %s\n",arg.c_str(),val.c_str());
      for (size_t j=0; j<params.size(); j++) {
        if (params[j].name==arg || params[j].shortName==arg) {
          if (!params[j].func(val)) return 1;
          break;
        }
      }
    } else {
      if (mdat=="") {
        mdat=argv[i];
      } else {
        if (smpl=="") {
          smpl=argv[i];
        }
      }
    }
  }

  if (mdat=="") {
    printf("usage: %s [-params] mdat.file [smpl.file]\n",argv[0]);
    return 1;
  }

  if (smpl=="") {
    size_t repPos=mdat.rfind("mdat");
    if (repPos==string::npos) {
      printf("cannot auto-locate smpl file. please provide it manually.\n");
      return 1;
    }
    smpl=mdat;
    smpl.replace(repPos,4,"smpl");
  }

  if (!p.load(mdat.c_str(),smpl.c_str())) {
    printf("could not open song...\n");
    return 1;
  }

#ifdef _SYNC_VBLANK
  if (syncVBlank) {
    syncfd=open("/dev/dri/card0",O_RDWR);
    if (syncfd<0) {
      perror("cannot open sync");
      return 1;
    }
    drm_version ver;
    if (ioctl(syncfd,DRM_IOCTL_VERSION,&ver)<0) {
      printf("could not get version.\n");
      return 1;
    }
  }
#endif

  if (dumpFile) {
    dump=fopen("tfmx.raw","wb");
    if (dump==NULL) {
      perror("cannot dump");
      return 1;
    }
  }

  printf("opening audio\n");
  
  SDL_Init(SDL_INIT_AUDIO);

  ac.freq=44100;
  ac.format=AUDIO_S16;
  ac.channels=2;
  ac.samples=1024;
  if (hle) {
    ac.callback=processHLE;
  } else {
    ac.callback=process;
  }
  ac.userdata=NULL;
  ai=SDL_OpenAudioDevice(SDL_GetAudioDeviceName(0,0),0,&ac,&ar,SDL_AUDIO_ALLOW_ANY_CHANGE);
  sr=ar.freq;
  if (ntsc) {
    targetSR=3579545;
    speed=59659;
  } else {
    targetSR=3546895;
    speed=70937;
  }
  p.setCIAVal(speed);

  bb[0]=blip_new(32768);
  bb[1]=blip_new(32768);
  blip_set_rates(bb[0],targetSR,sr);
  blip_set_rates(bb[1],targetSR,sr);

  p.hleRate=float((double)targetSR/(double)sr);

  printf("running.\n");
  p.play(songid);
  SDL_PauseAudioDevice(ai,0);
  
  sigemptyset(&intsa.sa_mask);
  intsa.sa_flags=0;
  intsa.sa_handler=handleTerm;
  sigaction(SIGINT,&intsa,NULL);
  
  setvbuf(stdin,NULL,_IONBF,1);
  if (tcgetattr(0,&termprop)!=0) {
    return 1;
  }
  memcpy(&termpropold,&termprop,sizeof(struct termios));
  termprop.c_lflag&=~ECHO;
  termprop.c_lflag&=~ICANON;
  if (tcsetattr(0,TCSAFLUSH,&termprop)!=0) {
    return 1;
  }
  
  //p.lock(0,2000000);
  //p.lock(1,2000000);
  //p.lock(3,2000000);

#ifdef _SYNC_VBLANK
  struct timespec vt1, vt2;
  drm_wait_vblank_t vblank;
  vt1=curTime(CLOCK_MONOTONIC);
  if (syncVBlank) while (!quit) {
    vblank.request.sequence=1;
    vblank.request.type=_DRM_VBLANK_RELATIVE;
    if (ioctl(syncfd,DRM_IOCTL_WAIT_VBLANK,&vblank)<0) continue;
    vt2=curTime(CLOCK_MONOTONIC);
    if ((vt2-vt1).tv_nsec>1000000) {
      p.setCIAVal(targetSR*((vt2-vt1).tv_nsec/1000)/1000000);
    }
    vt1=vt2;
  } else
#endif
  
  while (!quit) {
    int c;
    c=fgetc(stdin);
    if (c==EOF) break;
    switch (c) {
      case '\n':
        p.trace=!p.trace;
        printf("frame trace: %s\n",truth[p.trace]);
        break;
      case '\\':
        speed=70937*6;
        p.setCIAVal(speed);
        printf("CIA value: %d (%.2fHz)\n",speed,(double)targetSR/(double)speed);
        break;
      case '~':
        p.traceS=!p.traceS;
        printf("register trace: %s\n",truth[p.traceS]);
        break;
      case '\b': case 127:
        if (ntsc) {
          speed=59659;
        } else {
          speed=70937;
        }
        printf("CIA value: %d (%.2fHz)\n",speed,(double)targetSR/(double)speed);
        p.setCIAVal(speed);
        break;
      case '[':
        speed+=speed/11;
        printf("CIA value: %d (%.2fHz)\n",speed,(double)targetSR/(double)speed);
        p.setCIAVal(speed);
        break;
      case ']':
        speed-=speed/11;
        printf("CIA value: %d (%.2fHz)\n",speed,(double)targetSR/(double)speed);
        p.setCIAVal(speed);
        break;
      case '{':
        speed<<=1;
        printf("CIA value: %d (%.2fHz)\n",speed,(double)targetSR/(double)speed);
        p.setCIAVal(speed);
        break;
      case '}':
        speed>>=1;
        printf("CIA value: %d (%.2fHz)\n",speed,(double)targetSR/(double)speed);
        p.setCIAVal(speed);
        break;
      case '`':
        ntsc=!ntsc;
        printf("TV standard: %s\n",ntsc?("NTSC"):("PAL"));
        if (ntsc) {
          targetSR=3579545;
          speed=59659;
        } else {
          targetSR=3546895;
          speed=70937;
        }
        p.setCIAVal(speed);
        p.hleRate=float((double)targetSR/(double)sr);
        printf("CIA value: %d (%.2fHz)\n",speed,(double)targetSR/(double)speed);
        break;
      case '1':
        printf("channel 0 mute: %s\n",truth[p.mute(0)]);
        break;
      case '2':
        printf("channel 1 mute: %s\n",truth[p.mute(1)]);
        break;
      case '3':
        printf("channel 2 mute: %s\n",truth[p.mute(2)]);
        break;
      case '4':
        printf("channel 3 mute: %s\n",truth[p.mute(3)]);
        break;
      case '5':
        p.traceC[0]=!p.traceC[0];
        printf("channel 0 macro trace: %s\n",truth[p.traceC[0]]);
        break;
      case '6':
        p.traceC[1]=!p.traceC[1];
        printf("channel 1 macro trace: %s\n",truth[p.traceC[1]]);
        break;
      case '7':
        p.traceC[2]=!p.traceC[2];
        printf("channel 2 macro trace: %s\n",truth[p.traceC[2]]);
        break;
      case '8':
        p.traceC[3]=!p.traceC[3];
        printf("channel 3 macro trace: %s\n",truth[p.traceC[3]]);
        break;
      default:
        if (c>='A') {
          p.lock(3,32);
          p.playMacro(c-'A',20,15,3,0);
        }
        break;
    }
  }

  SDL_CloseAudioDevice(ai);

  if (dumpFile) {
    printf("closing dump\n");
    fflush(dump);
    fclose(dump);
    dumpFile=false;
  }
  
  printf("quit!\n");
  finish();
  return 0;
}
