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
    }
    printf("macro %d len %d\n",i,s);
  }
  
  fclose(f);
  
  chan[0].len=smplLen;
  chan[0].freq=1712/4;
  chan[0].vol=64;

  return true;
}

int TFMXPlayer::play(int song) {
  curSong=song;
  speed=head.songSpeed[song];
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
  /*
  chan[c].pos=25000;
  chan[c].len=25000+2048;
  chan[c].freq=getPeriod((note&63)+trans);
  chan[c].vol=64;*/
  
  cstat[c].index=macro;
  cstat[c].pos=0;
  cstat[c].tim=0;
  cstat[c].vol=vol;
  cstat[c].oldnote=cstat[c].note;
  cstat[c].note=(note&63)+trans;
}

void TFMXPlayer::updateRow(int row) {
  curRow=row;
  printf("Next Row!\n");
  if (track[curRow][0].pat==0xef && track[curRow][0].trans==0xfe) {
    printf("EFFE!!!\n");
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
    tstat[i].tim=0;
    tstat[i].pos=-1;
    tstat[i].index=track[curRow][i].pat;
    tstat[i].trans=track[curRow][i].trans;

    if (tstat[i].index==0xfe) chan[tstat[i].trans].on=false;
  }
  curTick=0;
  nextTick();
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
          tstat[tr].pos=-1;
          break;
        case pJump:
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
          break;
        case pVibr:
          break;
        case pEnve:
          break;
        case pGsPt:
          break;
        case pRoPt:
          break;
        case pFade:
          break;
        case pPPat:
          break;
        case pPort:
          break;
        case pLock:
          break;
        case pStCu:
          break;
        case pNOP:
          break;
        default:
          if (item.note&0x80) {
            playMacro(item.ins,item.note,item.vol,item.chan,tstat[tr].trans);
            tstat[tr].tim=item.detune;
            getMeOut=true;
          }
          break;
      }
    }
  }
  return false;
}

void TFMXPlayer::runMacro(int i) {
  TFMXMacroData m;
  cstat[i].tim=0;
  cstat[i].waitingDMA=false;
  while (true) {
    m=macro[cstat[i].index][cstat[i].pos];
    cstat[i].pos++;
    switch (m.op) {
      case mOffReset:
        chan[i].freq=0;
        chan[i].pos=0;
        chan[i].apos=0;
        chan[i].on=false;
        cstat[i].addBeginC=0;
        cstat[i].addBeginDir=false;
        return;
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
      case mAddVol:
        chan[i].vol=m.data[2]+cstat[i].vol;
        break;
      case mSetNote:
        // TODO detune
        chan[i].freq=getPeriod(m.data[0]+3);
        break;
      case mAddNote:
        chan[i].freq=getPeriod(cstat[i].note+(signed char)m.data[0]+3);
        break;
      case mSetPrevNote:
        chan[i].freq=getPeriod(cstat[i].oldnote+(signed char)m.data[0]+3);
        break;
      case mOn:
        chan[i].on=true;
        return;
        break;
      case mWait:
        cstat[i].tim=(m.data[0]<<16)|(m.data[1]<<8)|(m.data[2]);
        return;
        break;
      case mWaitUp:
        cstat[i].tim=2147483647;
        return;
        break;
      case mStop:
        cstat[i].tim=2147483647;
        return;
        break;
      case mAddBegin:
        // TODO: corruption bug!
        cstat[i].addBegin=m.data[0];
        cstat[i].addBeginC=m.data[0];
        cstat[i].addBeginAmt=(signed short)((m.data[1]<<8)|(m.data[2]));
        if (cstat[i].addBeginAmt<0) {
          cstat[i].addBeginAmt=-cstat[i].addBeginAmt;
          cstat[i].addBeginDir=true;
        } else {
          cstat[i].addBeginDir=false;
        }
        printf("%.2x %.2x %.2x %.2x... %d\n",m.op,m.data[0],m.data[1],m.data[2],cstat[i].addBeginAmt);
        break;
      case mSetLoop:
        cstat[i].postDMAPos=chan[i].pos+((m.data[1]<<8)|(m.data[2]));
        cstat[i].postDMALen=chan[i].len-(((m.data[1]<<8)|(m.data[2]))>>1);
        break;
      case mWaitSample:
        cstat[i].tim=2147483647;
        cstat[i].waitingDMA=true;
        return;
        break;
      default:
        printf("%d: unhandled opcode %x\n",i,m.op);
        return;
        break;
    }
  }
}

void TFMXPlayer::nextTick() {
  // update macros
  for (int i=0; i<4; i++) {
    if (cstat[i].index!=-1) {
      if (cstat[i].addBeginC>0) {
        if (cstat[i].addBeginDir) {
          chan[i].pos-=cstat[i].addBeginAmt;
        } else {
          chan[i].pos+=cstat[i].addBeginAmt;
        }
        if (--cstat[i].addBegin<0) {
          cstat[i].addBegin=cstat[i].addBeginC;
          cstat[i].addBeginDir=!cstat[i].addBeginDir;
        }
      }
      if (--cstat[i].tim>=0) continue;
      runMacro(i);
    }
  }
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
        break;
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
  if (cstat[c].waitingDMA) {
    printf("%d: DMA reach\n",c);
    runMacro(c);
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
    if (chan[i].freq) {
#ifdef HLE
      chan[i].seek-=intAccum;
#else
      --chan[i].seek;
#endif
      if (chan[i].seek<0) {
#ifdef HLE
        chan[i].seek+=chan[i].freq;
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
      la+=smpl[chan[i].pos+chan[i].apos]*chan[i].vol<<1;
    } else {
      ra+=smpl[chan[i].pos+chan[i].apos]*chan[i].vol<<1;
    }
  }
  *l=la;
  *r=ra;
}

void TFMXPlayer::setCIAVal(int val) {
  ciaVal=val;
}

