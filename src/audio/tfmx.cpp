#include "tfmx.h"

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
  printf("Next Row!\n");
  if (track[curRow][0].pat==0xef && track[curRow][0].trans==0xfe) {
    printf("EFFE!!! (%d)\n",track[curRow][1].trans);
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
  }
}

bool TFMXPlayer::updateTrack(int tr) {
  TFMXPatData item;
  bool getMeOut=false;
  if (--tstat[tr].tim<0) {
    // read next item
    while (!getMeOut) {
      tstat[tr].pos++;
      item=pat[tstat[tr].index][tstat[tr].pos];
      printf("%d: chan %d note %.2x\n",tr,item.chan,item.note);
      switch (item.note) {
        case pEnd:
          printf("returning end\n");
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
          printf("unhandled jump\n");
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
          printf("vibrato!\n");
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
            printf("PORTA\n");
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
    cstat[i].pos++;
    switch (m.op) {
      case mOffReset:
        if (m.data[0]|m.data[1]|m.data[2]) {
          printf("\x1b[1;31m%d: immediate!\x1b[m\n",i);
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
          printf("%d: call to delayed pos (%d)\n",i,cstat[i].postDMAPos);
        } else {
          chan[i].pos=(m.data[0]<<16)|(m.data[1]<<8)|(m.data[2]);
          chan[i].apos=0;
          printf("%d: call to pos (%d)\n",i,chan[i].pos);
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
        chan[i].vol=m.data[2]+cstat[i].vol*3;
        break;
      case mSetVol:
        chan[i].vol=m.data[2];
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
        printf("%d: waiting on key up.\n",i);
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
        //printf("%d: %.2x %.2x %.2x %.2x... %d AT THE BEGINNING: %d\n",i,m.op,m.data[0],m.data[1],m.data[2],cstat[i].addBeginAmt,chan[i].pos);
        break;
      case mSetLoop:
        cstat[i].addBeginC=0;
        cstat[i].postDMAPos=chan[i].pos+((m.data[1]<<8)|(m.data[2]));
        cstat[i].postDMALen=chan[i].len-(((m.data[1]<<8)|(m.data[2]))>>1);
        printf("%d: call to set loop (%d)\n",i,cstat[i].postDMAPos);
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
    if (chan[i].vol>0x40) printf("%d: volume too high! (%.2x)\n",i,chan[i].vol);
    
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
    cstat[c].waitingDMA--;
    printf("%d: DMA reach\n",c);
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
