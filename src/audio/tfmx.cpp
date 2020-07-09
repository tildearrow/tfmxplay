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
  smpl=new char[smplLen];
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

// 1400.0f
unsigned short getPeriod(unsigned char note) {
  return 2440.6f/(pow(2,(float)note/12.0f));
}

void TFMXPlayer::playMacro(char macro, char note, char vol, unsigned char c, int trans) {
  /*
  chan[c].pos=25000;
  chan[c].len=25000+2048;
  chan[c].freq=getPeriod((note&63)+trans);
  chan[c].vol=64;*/
  
  cstat[c].index=macro;
  cstat[c].pos=0;
  cstat[c].tim=0;
  cstat[c].vol=vol;
  cstat[c].note=(note&63)+trans;
}

void TFMXPlayer::updateRow(int row) {
  curRow=row;
  printf("Next Row!\n");
  for (int i=0; i<8; i++) {
    tstat[i].tim=0;
    tstat[i].pos=-1;
    tstat[i].index=track[curRow][i].pat;
    tstat[i].trans=track[curRow][i].trans;
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

void TFMXPlayer::nextTick() {
  // update macros
  bool getMeOut;
  for (int i=0; i<4; i++) {
    if (cstat[i].index!=-1) {
      TFMXMacroData m;
      if (--cstat[i].tim>=0) continue;
      getMeOut=false;
      cstat[i].tim=0;
      while (!getMeOut) {
        m=macro[cstat[i].index][cstat[i].pos];
        switch (m.op) {
          case mOffReset:
            chan[i].freq=0;
            chan[i].pos=0;
            chan[i].apos=0;
            chan[i].loop=0xffff;
            getMeOut=true;
            break;
          case mSetBegin:
            chan[i].pos=(m.data[0]<<16)|(m.data[1]<<8)|(m.data[2]);
            chan[i].apos=0;
            break;
          case mSetLen:
            chan[i].len=(m.data[1]<<8)|(m.data[2]);
            break;
          case mAddVol:
            chan[i].vol=m.data[2]+cstat[i].vol;
            break;
          case mSetNote:
            // TODO detune
            chan[i].freq=getPeriod(m.data[0]+6);
            break;
          case mAddNote:
            chan[i].freq=getPeriod(cstat[i].note+(char)m.data[0]+6);
            break;
          case mSetPrevNote:
            chan[i].freq=getPeriod(cstat[i].note+(char)m.data[0]+6);
            break;
          case mOn:
            // TODO
            chan[i].apos=0;
            getMeOut=true;
            break;
          case mWait:
            cstat[i].tim=(m.data[0]<<16)|(m.data[1]<<8)|(m.data[2]);
            getMeOut=true;
            break;
          case mWaitUp:
            cstat[i].tim=2147483647;
            getMeOut=true;
            break;
          case mStop:
            cstat[i].tim=2147483647;
            getMeOut=true;
            break;
          case mSetLoop:
            chan[i].loop=(m.data[1]<<8)|(m.data[2]);
            break;
          case mWaitSample:
            printf("warning: wait on DMA not done yet\n");
            cstat[i].tim=2147483647;
            getMeOut=true;
            break;
          default:
            printf("%d: unhandled opcode %x\n",i,m.op);
            getMeOut=true;
            break;
        }
        cstat[i].pos++;
      }
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

void TFMXPlayer::nextSample(short* l, short* r) {
  int la, ra;
  la=0; ra=0;
  if (--ciaCount<0) {
    ciaCount=ciaVal;
    nextTick();
  }
  
  for (int i=0; i<4; i++) {
    if (chan[i].freq) {
      if (--chan[i].seek<0) {
        chan[i].seek=chan[i].freq;
        chan[i].apos++;
        if (chan[i].apos>=chan[i].len) {
          if (chan[i].loop!=0xffff) {
            chan[i].apos=chan[i].loop;
          }
        }
      }
    }
    la+=smpl[chan[i].pos+chan[i].apos]*chan[i].vol;
  }
  *l=la;
  *r=la;
}
