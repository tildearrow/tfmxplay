#include "tfmx.h"

const char* macroName[]={
  "OffReset",
  "On",
  "SetBegin",
  "SetLen",
  "Wait",
  "Loop",
  "Cont",
  "Stop",
  "AddNote",
  "SetNote",
  "Reset",
  "Porta",
  "Vibrato",
  "AddVol",
  "SetVol",
  "Env",
  "LoopUp",
  "AddBegin",
  "AddLen",
  "Off",
  "WaitUp",
  "GoSub",
  "Ret",
  "SetPeriod",
  "SetLoop",
  "OneShot",
  "WaitDMA",
  "Rand",
  "SplitKey",
  "SplitVol",
  "AddVolNote",
  "SetPrevNote",
  "Signal",
  "PlayMacro"
};

const char* noteName[]={
  "F#0", "G-0", "G#0", "A-0", "A#0", "B-0",
  "C-1", "C#1", "D-1", "D#1", "E-1", "F-1", "F#1", "G-1", "G#1", "A-1", "A#1", "B-1",
  "C-2", "C#2", "D-2", "D#2", "E-2", "F-2", "F#2", "G-2", "G#2", "A-2", "A#2", "B-2",
  "C-3", "C#3", "D-3", "D#3", "E-3", "F-3", "F#3", "G-3", "G#3", "A-3", "A#3", "B-3",
  "C-4", "C#4", "D-4", "D#4", "E-4", "F-4", "F#4", "G-4", "G#4", "A-4", "A#4", "B-4",
  "C-5", "C#5", "D-5", "D#5", "E-5", "F-5", "F#5", "G-5", "G#5", "A-5", "A#5", "B-5",
};

bool TFMXPlayer::load(const char* mdata, const char* smpla) {
  FILE* f;
  // smpl
  printf("reading samples...\n");
  f=fopen(smpla,"r");
  if (f==NULL) {
    perror("fail");
    return false;
  }
  fseek(f,0,SEEK_END);
  smplLen=ftell(f);
  fseek(f,0,SEEK_SET);
  smpl=new signed char[smplLen];
  fread(smpl,1,smplLen,f);
  fclose(f);

  // mdat
  printf("reading song data...\n");
  f=fopen(mdata,"r");
  if (f==NULL) {
    perror("fail");
    return false;
  }
  // read header
  if (fread(&head,1,512,f)!=512) {
    printf("fail: Incomplete header\n");
    fclose(f);
    return false;
  }
  // endianness fix
  head.ordSeek=ntohl(head.ordSeek);
  head.patSeek=ntohl(head.patSeek);
  head.macroSeek=ntohl(head.macroSeek);
  for (int i=0; i<32; i++) {
    head.songStart[i]=ntohs(head.songStart[i]);
    head.songEnd[i]=ntohs(head.songEnd[i]);
    head.songSpeed[i]=ntohs(head.songSpeed[i]);
  }
  
  if (head.ordSeek==0) head.ordSeek=0x800;
  if (head.patSeek==0) head.patSeek=0x400;
  if (head.macroSeek==0) head.macroSeek=0x600;
  
  // read pattern pointers
  fseek(f,head.patSeek,SEEK_SET);
  fread(patPoint,4,128,f);
  for (int i=0; i<128; i++) {
    patPoint[i]=ntohl(patPoint[i]);
  }
  
  // read macro pointers
  fseek(f,head.macroSeek,SEEK_SET);
  fread(macroPoint,4,128,f);
  for (int i=0; i<128; i++) {
    macroPoint[i]=ntohl(macroPoint[i]);
  }
  
  // read orders
  fseek(f,head.ordSeek,SEEK_SET);
  fread(track,1,2*8*128,f);
  
  // read patterns
  int s=0;
  for (int i=0; i<128; i++) {
    fseek(f,patPoint[i],SEEK_SET);
    s=0;
    while (true) {
      fread(&pat[i][s],sizeof(TFMXPatData),1,f);
      if (pat[i][s].note==0xf0) break;
      s++;
      if (s>255) break;
    }
    printf("pat %d len %d\n",i,s);
  }
  
  // read macros
  for (int i=0; i<128; i++) {
    fseek(f,macroPoint[i],SEEK_SET);
    s=0;
    while (true) {
      fread(&macro[i][s],sizeof(TFMXMacroData),1,f);
      if (macro[i][s].op==7) break;
      s++;
      if (s>255) break;
    }
    printf("macro %d len %d\n",i,s);
  }
  
  fclose(f);

  return true;
}

int TFMXPlayer::play(int song) {
  curSong=song;
  speed=head.songSpeed[song];

  for (int i=0; i<4; i++) {
    reset(i);
  }

  updateRow(head.songStart[curSong]);
  
  return curRow;
}

int TFMXPlayer::stop() {
  exit(0);
  return curRow;
}

// 1400.0f
unsigned short getPeriod(unsigned char note) {
  return 2034/(pow(2,(float)note/12.0f));
}

void TFMXPlayer::playMacro(signed char macro, signed char note, signed char vol, unsigned char c, int trans) {
  cstat[c].index=macro;
  cstat[c].pos=0;
  cstat[c].tim=0;
  cstat[c].vol=vol;
  cstat[c].oldnote=cstat[c].note;
  cstat[c].note=(note&63)+trans;
  cstat[c].keyon=true;
  cstat[c].waitingDMA=0;
  cstat[c].waitingKeyUp=false;
}

void TFMXPlayer::updateRow(int row) {
  short newTempo;
  curRow=row;
  curStep=0;
  printf("\x1b[1;34m-----[ Next Row! ]-----\x1b[m\n");
  printf("\x1b[36m%d:",curRow);
  for (int i=0; i<8; i++) {
    printf(" %.2x%.2x",track[curRow][i].pat,track[curRow][i].trans);
  }
  printf("\x1b[m\n");
  if (track[curRow][0].pat==0xef && track[curRow][0].trans==0xfe) {
    switch (track[curRow][1].trans) {
      case 0: // stop
        stop();
        return;
        break;
      case 1: // jump
        updateRow(track[curRow][2].trans);
        return;
        break;
      case 2: // tempo
        printf("tempo\n");
        newTempo=(track[curRow][3].pat<<8)+(track[curRow][3].trans);
        if (newTempo>0) {
          ciaVal=8948863.63/newTempo;
        }
        break;
      case 3: // volslide
        break;
      case 4: // volslide
        break;
    }
    updateRow(row+1);
    return;
  }
  for (int i=0; i<8; i++) {
    if (track[curRow][i].pat==0x80) continue;
    tstat[i].tim=-1;
    tstat[i].pos=-1;
    tstat[i].index=track[curRow][i].pat;
    tstat[i].trans=track[curRow][i].trans;
    tstat[i].loopCount=0;

    if (tstat[i].index==0xfe) chan[tstat[i].trans].on=false;

    if (tstat[i].index<128 && totTracks<(i+1)) totTracks=i+1;
  }
}

void TFMXPlayer::printItem(TFMXPatData item) {
  switch (item.note&0xc0) {
    case 0x00: case 0x40:
      printf(" %s~%.2x~%.1x%.1x~%.2x",noteName[item.note&0x3f],item.ins,item.vol,item.chan,item.detune);
      break;
    case 0x80:
      printf(" %s %.2x %.1x%.1x %.2x",noteName[item.note&0x3f],item.ins,item.vol,item.chan,item.detune);
      break;
    case 0xc0:
      if (item.note>=0xf0) {
        switch (item.note) {
          case pEnd:
            printf(" ----NEXT----");
            break;
          case pLoop:
            printf(" Loop %.2x:%.1x%.1x%.2x",item.ins,item.vol,item.chan,item.detune);
            break;
          case pJump:
            printf(" Jump %.2x:%.1x%.1x%.2x",item.ins,item.vol,item.chan,item.detune);
            break;
          case pWait:
            printf(" --Wait %.2x'--",item.ins);
            break;
          case pStop:
            printf(" ----STOP----");
            break;
          case pKeyUp:
            printf("   ^^^^^^   %.1x",item.chan);
            break;
          case pVibr:
            printf(" Vibr %.2x:%.2x %.1x",item.ins,item.detune,item.chan);
            break;
          case pEnve:
            printf(" Env %.2x%.2x %.1x %.1x",item.ins,item.detune,item.vol,item.chan);
            break;
          case pGsPt:
            printf(" GsPt %.2x:%.1x%.1x%.2x",item.ins,item.vol,item.chan,item.detune);
            break;
          case pRoPt:
            printf(" ---Return---");
            break;
          case pFade:
            printf(" Fade %.2x : %.2x",item.ins,item.detune);
            break;
          case pPPat:
            printf(" PPat %.2x:%.2x %.1x",item.ins,item.detune,item.chan);
            break;
          case pPort:
            printf(" Port %.2x:%.2x %.1x",item.ins,item.detune,item.chan);
            break;
          case pLock:
            printf(" Lock %.2x:%.1x%.1x%.2x",item.ins,item.vol,item.chan,item.detune);
            break;
          case pStCu:
            printf(" -StopCustom-");
            break;
          case pNOP:
            printf(" ------------");
            break;
        }
      } else {
        printf(" %s %.2x %.1x%.1x %.2x",noteName[item.note&0x3f],item.ins,item.vol,item.chan,item.detune);
      }
      break;
  }
}

void TFMXPlayer::dumpPat() {
  printf("%3d",curStep);
  printf("\x1b[1m");
  if (totTracks>5) {
    for (int i=0; i<totTracks; i++) {
      if (patChanged[i]) {
        printf("\x1b[31m");
        patChanged[i]=false;
      } else {
        printf("\x1b[30m");
      }
      if (tstat[i].index>=128) {
        printf(" --------");
      } else {
        printf(" %.2x%.2x%.1x%.1x%.2x",curPat[i].note,curPat[i].ins,curPat[i].vol,curPat[i].chan,curPat[i].detune);
      }
    }
  } else {
    for (int i=0; i<totTracks; i++) {
      if (patChanged[i]) {
        printf("\x1b[31m");
        patChanged[i]=false;
      } else {
        printf("\x1b[30m");
      }
      if (tstat[i].index>=128) {
        printf(" ------------");
      } else {
        printItem(curPat[i]);
      }
    }
  }
  printf("\x1b[m\n");
}

bool TFMXPlayer::updateTrack(int tr) {
  TFMXPatData item;
  bool getMeOut=false;
  if (--tstat[tr].tim<0) {
    // read next item
    while (!getMeOut) {
      tstat[tr].pos++;
      item=pat[tstat[tr].index][tstat[tr].pos];
      curPat[tr]=item;
      patChanged[tr]=true;
      switch (item.note) {
        case pEnd:
          return true;
          break;
        case pLoop:
          if (item.ins==0) {
            tstat[tr].pos=((item.vol<<12)|(item.chan<<8)|item.detune)-1;
          } else {
            if (tstat[tr].loopCount==0) {
              tstat[tr].loopCount=item.ins+1;
            }
            if (--tstat[tr].loopCount!=0) {
              tstat[tr].pos=((item.vol<<12)|(item.chan<<8)|item.detune)-1;
            }
          }
          break;
        case pJump:
          tstat[tr].index=item.ins;
          tstat[tr].pos=((item.vol<<12)|(item.chan<<8)|item.detune)-1;
          break;
        case pWait:
          tstat[tr].tim=item.ins;
          getMeOut=true;
          break;
        case pStop:
          tstat[tr].tim=0x7fffffff;
          getMeOut=true;
          break;
        case pKeyUp:
          if (!cstat[item.chan].locked) {
            cstat[item.chan].keyon=false;
          }
          break;
        case pVibr:
          if (!cstat[item.chan].locked) {
            cstat[item.chan].vibTimeC=item.ins;
            cstat[item.chan].vibTime=item.ins>>1;
            cstat[item.chan].vibAmt=item.detune;
            cstat[item.chan].vibDir=false;
            cstat[item.chan].detune=0;
          }
          break;
        case pEnve:
          if (!cstat[item.chan].locked) {
            cstat[item.chan].envActive=true;
            cstat[item.chan].envAmt=item.ins;
            cstat[item.chan].envTime=item.vol+1;
            cstat[item.chan].envTimeC=item.vol+1;
            cstat[item.chan].envTarget=item.detune;
          }
          break;
        case pGsPt:
          printf("unhandled gosub\n");
          break;
        case pRoPt:
          printf("unhandled return\n");
          break;
        case pFade:
          printf("unhandled fade\n");
          break;
        case pPPat:
          printf("unhandled playpat\n");
          break;
        case pPort:
          printf("unhandled porta\n");
          break;
        case pLock:
          printf("unhandled lock\n");
          break;
        case pStCu:
          printf("custom stop\n");
          tstat[tr].tim=0x7fffffff;
          getMeOut=true;
          break;
        case pNOP:
          break;
        default:
          if ((item.note&0xc0)==0x80) {
            if (!cstat[item.chan].locked) {
              playMacro(item.ins,item.note,item.vol,item.chan,tstat[tr].trans);
            }
            tstat[tr].tim=item.detune;
            getMeOut=true;
          } else if ((item.note&0xc0)==0xc0) {
            if (!cstat[item.chan].locked) {
              cstat[item.chan].portaActive=true;
              cstat[item.chan].portaTimeC=item.ins;
              cstat[item.chan].portaTime=0;
              cstat[item.chan].portaTarget=(item.note&63)+3+tstat[tr].trans;
              if (getPeriod(cstat[item.chan].portaTarget)<cstat[item.chan].freq) {
                cstat[item.chan].portaAmt=-item.detune;
              } else {
                cstat[item.chan].portaAmt=item.detune;
              }
            }
          } else {
            if (!cstat[item.chan].locked) {
              playMacro(item.ins,item.note,item.vol,item.chan,tstat[tr].trans);
            }
          }
          break;
      }
      if (!getMeOut) dumpPat();
    }
  }
  return false;
}

void TFMXPlayer::reset(int i) {
  cstat[i].offReset=false;
  cstat[i].freq=0;
  cstat[i].detune=0;
  chan[i].pos=0;
  chan[i].apos=0;
  chan[i].on=false;
  chan[i].looping=true;
  cstat[i].addBeginC=0;
  cstat[i].addBeginDir=false;
  cstat[i].vibTimeC=0;
  cstat[i].vibDir=false;
  cstat[i].envActive=false;
  cstat[i].portaActive=false;
  cstat[i].postDMAPos=-1;
  cstat[i].postDMALen=-1;
  cstat[i].postDMAAdd=0;
  cstat[i].waitingDMA=0;
  cstat[i].waitingKeyUp=false;
}

void TFMXPlayer::runMacro(int i) {
  TFMXMacroData m;
  cstat[i].tim=0;
  cstat[i].waitingDMA=0;
  cstat[i].waitingKeyUp=false;
  if (cstat[i].imm) {
    cstat[i].imm=false;
    reset(i);
  }
  while (true) {
    m=macro[cstat[i].index][cstat[i].pos];
    if (trace) printf("\x1b[1;33m%d: %.2x: %s %.2x%.2x%.2x\x1b[m\n",i,cstat[i].pos,macroName[m.op],m.data[0],m.data[1],m.data[2]);
    cstat[i].pos++;
    switch (m.op) {
      case mOffReset:
        if (m.data[0]|m.data[1]|m.data[2]) {
          cstat[i].imm=true;
          return;
        } else {
          cstat[i].offReset=true;
          return;
        }
        break;
      case mSetBegin:
        if (chan[i].on) {
          cstat[i].postDMAPos=(m.data[0]<<16)|(m.data[1]<<8)|(m.data[2]);
        } else {
          chan[i].pos=(m.data[0]<<16)|(m.data[1]<<8)|(m.data[2]);
          chan[i].apos=0;
        }
        break;
      case mSetLen:
        if (chan[i].on) {
          cstat[i].postDMALen=(m.data[1]<<8)|(m.data[2]);
        } else {
          chan[i].len=(m.data[1]<<8)|(m.data[2]);
        }
        break;
      case mLoop:
        printf("%d: call to loop\n",i);
        cstat[i].pos=((m.data[1]<<8)|(m.data[2]));
        printf("new pos: %d\n",cstat[i].pos);
        break;
      case mLoopUp:
        printf("%d: call to loop UP!!!\n",i);
        if (cstat[i].keyon) {
          cstat[i].pos=((m.data[1]<<8)|(m.data[2]));
        }
        break;
      case mAddVol:
        if (chan[i].on) {
          chan[i].nextvol=m.data[2]+cstat[i].vol*3;
          cstat[i].changeVol=true;
          if (trace) printf("\x1b[1;32mPOST.\x1b[m\n");
        } else {
          chan[i].vol=m.data[2]+cstat[i].vol*3;
        }
        break;
      case mSetVol:
        if (chan[i].on) {
          chan[i].nextvol=m.data[2];
          cstat[i].changeVol=true;
          if (trace) printf("\x1b[1;32mPOST.\x1b[m\n");
        } else {
          chan[i].vol=m.data[2];
        }
        break;
      case mSetNote:
        // TODO detune
        cstat[i].freq=getPeriod(m.data[0]+3);
        if (chan[i].on) return;
        break;
      case mAddNote:
        cstat[i].freq=getPeriod(cstat[i].note+(signed char)m.data[0]+3);
        if (chan[i].on) return;
        break;
      case mSetPeriod:
        cstat[i].freq=(m.data[1]<<8)|(m.data[2]);
        return;
        break;
      case mSetPrevNote:
        cstat[i].freq=getPeriod(cstat[i].oldnote+(signed char)m.data[0]+3);
        return;
        break;
      case mOn:
        chan[i].on=true;
        break;
      case mVibrato:
        cstat[i].vibTimeC=m.data[0];
        cstat[i].vibTime=m.data[0]>>1;
        cstat[i].vibAmt=m.data[2];
        cstat[i].vibDir=false;
        cstat[i].detune=0;
        break;
      case mPorta:
        printf("MPORTA\n");
        cstat[i].portaActive=true;
        cstat[i].portaTime=0;
        cstat[i].portaTimeC=m.data[0];
        cstat[i].portaAmt=(signed short)((m.data[1]<<8)|(m.data[2]));
        if (cstat[i].portaAmt==0 || cstat[i].portaTimeC==0) cstat[i].portaActive=false;
        break;
      case mEnv:
        cstat[i].envActive=true;
        cstat[i].envAmt=m.data[0];
        cstat[i].envTime=m.data[1];
        cstat[i].envTimeC=m.data[1];
        cstat[i].envTarget=m.data[2];
        break;
      case mWait:
        cstat[i].tim=(m.data[0]<<16)|(m.data[1]<<8)|(m.data[2]);
        return;
        break;
      case mWaitUp:
        if (cstat[i].keyon) {
          cstat[i].waitingKeyUp=true;
        }
        return;
        break;
      case mStop:
        cstat[i].tim=2147483647;
        return;
        break;
      case mAddBegin:
        cstat[i].addBegin=m.data[0];
        cstat[i].addBeginC=m.data[0];
        cstat[i].addBeginAmt=(signed short)((m.data[1]<<8)|(m.data[2]));
        if (cstat[i].addBeginAmt<0) {
          cstat[i].addBeginAmt=-cstat[i].addBeginAmt;
          cstat[i].addBeginDir=true;
        } else {
          cstat[i].addBeginDir=false;
        }
        break;
      case mSetLoop:
        cstat[i].addBeginC=0;
        cstat[i].postDMAPos=chan[i].pos+((m.data[1]<<8)|(m.data[2]));
        cstat[i].postDMALen=chan[i].len-(((m.data[1]<<8)|(m.data[2]))>>1);
        break;
      case mWaitSample:
        cstat[i].tim=2147483647;
        cstat[i].waitingDMA=((m.data[1]<<8)|(m.data[2]))+1;
        return;
        break;
      case mOneShot:
        chan[i].looping=false;
        break;
      default:
        printf("%d: unhandled opcode %x\n",i,m.op);
        return;
        break;
    }
  }
}

void TFMXPlayer::nextTick() {
  if (trace) printf("\x1b[1;36m--- FRAME %d ---\x1b[m\n",frame++);
  if (--curTick<0) {
    curTick=speed;
    for (int i=0; i<8; i++) {
      if (tstat[i].index<0x80) if (updateTrack(i)) {
        if (curRow>=head.songEnd[curSong]) {
          printf("end of song reached!\n");
          updateRow(head.songStart[curSong]);
        } else {
          updateRow(curRow+1);
        }
        i=-1;
      }
    }
    for (int i=0; i<8; i++) {
      if (patChanged[i]) {
        dumpPat();
        break;
      }
    }
    curStep++;
  }
  // update macros
  for (int i=0; i<4; i++) {
    if (cstat[i].index!=-1) {
      if (cstat[i].addBeginC>0) {
        if (cstat[i].addBeginDir) {
          cstat[i].postDMAAdd-=cstat[i].addBeginAmt;
        } else {
          cstat[i].postDMAAdd+=cstat[i].addBeginAmt;
        }
        if (--cstat[i].addBegin<0) {
          cstat[i].addBegin=cstat[i].addBeginC;
          cstat[i].addBeginDir=!cstat[i].addBeginDir;
        }
      }
      if (cstat[i].vibTimeC>0) {
        if (cstat[i].vibDir) {
          cstat[i].detune+=cstat[i].vibAmt;
        } else {
          cstat[i].detune-=cstat[i].vibAmt;
        }
        if (--cstat[i].vibTime<=0) {
          cstat[i].vibTime=cstat[i].vibTimeC;
          cstat[i].vibDir=!cstat[i].vibDir;
        }
      }
      if (cstat[i].envActive) {
        if (--cstat[i].envTime<0) {
          cstat[i].envTime=cstat[i].envTimeC;
          if (chan[i].vol>cstat[i].envTarget) {
            chan[i].vol-=cstat[i].envAmt;
            if (chan[i].vol<=cstat[i].envTarget) {
              chan[i].vol=cstat[i].envTarget;
              cstat[i].envActive=false;
            }
          } else {
            chan[i].vol+=cstat[i].envAmt;
            if (chan[i].vol>=cstat[i].envTarget) {
              chan[i].vol=cstat[i].envTarget;
              cstat[i].envActive=false;
            }
          }
        }
      }
      if (cstat[i].portaActive) {
        if (--cstat[i].portaTime<=0) {
          cstat[i].portaTime=cstat[i].portaTimeC;
          if (cstat[i].portaTimeC==0) cstat[i].portaActive=false;
          cstat[i].freq=(cstat[i].freq*(256+cstat[i].portaAmt))>>8;
          if (cstat[i].portaTarget!=-1) {
            if (cstat[i].portaAmt>0) {
              if (cstat[i].freq>getPeriod(cstat[i].portaTarget)) {
                cstat[i].freq=getPeriod(cstat[i].portaTarget);
                cstat[i].note=cstat[i].portaTarget;
                cstat[i].portaActive=false;
              }
            } else {
              if (cstat[i].freq<getPeriod(cstat[i].portaTarget)) {
                cstat[i].freq=getPeriod(cstat[i].portaTarget);
                cstat[i].note=cstat[i].portaTarget;
                cstat[i].portaActive=false;
              }
            }
          }
        }
      }
      if (cstat[i].changeVol) {
        cstat[i].changeVol=false;
        chan[i].vol=chan[i].nextvol;
      }
      if (cstat[i].waitingDMA) continue;
      if (cstat[i].waitingKeyUp && cstat[i].keyon) continue;
      if (--cstat[i].tim>=0) continue;
      runMacro(i);
      if (cstat[i].offReset) {
        reset(i);
      }
    }
  }
  // update freqs
  for (int i=0; i<4; i++) {
    chan[i].freq=(float)cstat[i].freq*pow(2,(float)cstat[i].detune/(6*256.0f));
    //if (chan[i].vol>0x40) printf("%d: volume too high! (%.2x)\n",i,chan[i].vol);
    
    if (cstat[i].locked) {
      if (--cstat[i].lockTime<=0) {
        cstat[i].locked=false;
      }
    }
  }
}

void TFMXPlayer::handleLoop(int c) {
  if (cstat[c].postDMAPos!=-1) {
    chan[c].pos=cstat[c].postDMAPos;
    cstat[c].postDMAPos=-1;
  }
  if (cstat[c].postDMALen!=-1) {
    chan[c].len=cstat[c].postDMALen;
    cstat[c].postDMALen=-1;
  }
  if (cstat[c].postDMAAdd!=0) {
    chan[c].pos+=cstat[c].postDMAAdd;
    cstat[c].postDMAAdd=0;
  }
  if (cstat[c].waitingDMA>0) {
    if (trace) printf("\x1b[36m- subtick %d/%d\x1b[m\n",ciaVal-ciaCount,ciaVal);
    cstat[c].waitingDMA--;
    if (trace) printf("%d: DMA reach\n",c);
    if (cstat[c].waitingDMA==0) {
      runMacro(c);
    }
  }
  if (!chan[c].looping) {
    chan[c].on=false;
  }
}

void TFMXPlayer::nextSample(short* l, short* r) {
  int la, ra;
  la=0; ra=0;
#ifdef HLE
  fractAccum+=hleRate;
  intAccum=fractAccum;
  fractAccum-=intAccum;

  ciaCount-=intAccum;
  if (ciaCount<0) {
    ciaCount+=ciaVal;
    nextTick();
  }
#else
  if (--ciaCount<0) {
    ciaCount=ciaVal;
    nextTick();
  }
#endif
  
  for (int i=0; i<4; i++) {
    if (!chan[i].on) continue;
    if (chan[i].freq>=100) {
#ifdef HLE
      chan[i].seek-=intAccum;
#else
      --chan[i].seek;
#endif
      if (chan[i].seek<0) {
#ifdef HLE
        chan[i].seek+=chan[i].freq+1;
#else
        chan[i].seek=chan[i].freq;
#endif
        chan[i].apos++;
        if (chan[i].apos>=(chan[i].len*2)) {
          // interrupt
          handleLoop(i);
          chan[i].apos=0;
        }
      }
    }
    if (i==0 || i==3) {
      la+=(smpl[chan[i].pos+chan[i].apos]*chan[i].vol);
    } else {
      ra+=(smpl[chan[i].pos+chan[i].apos]*chan[i].vol);
    }
  }
  *l=la;
  *r=ra;
}

void TFMXPlayer::setCIAVal(int val) {
  ciaVal=val;
}

void TFMXPlayer::lock(int chan, int t) {
  cstat[chan].locked=true;
  cstat[chan].lockTime=t;
}
