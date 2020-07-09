#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <arpa/inet.h>

struct TFMXHeader {
  char ident[10];
  short undoc1;
  int undoc2;
  char desc[240];
  short songStart[32];
  short songEnd[32];
  short songSpeed[32];
  char undoc3[16];
  int ordSeek;
  int patSeek;
  int macroSeek;
  int undoc4;
  char undoc5[32];
};

struct TFMXOrders {
  unsigned char pat;
  unsigned char trans;
};

struct TFMXPatData {
  unsigned char note;
  unsigned char ins;
  unsigned char chan: 4, vol: 4;
  unsigned char detune;
};

struct TFMXMacroData {
  unsigned char op;
  unsigned char data[3];
};

enum TFMXPatOps {
  pEnd=0xf0,
  pLoop,
  pJump,
  pWait,
  pStop,
  pKeyUp,
  pVibr,
  pEnve,
  pGsPt,
  pRoPt,
  pFade,
  pPPat,
  pPort,
  pLock,
  pStCu,
  pNOP
};

enum TFMXMacroOps {
  mOffReset=0,
  mOn,
  mSetBegin,
  mSetLen,
  mWait,
  mLoop,
  mCont,
  mStop,
  mAddNote,
  mSetNote,
  mReset,
  mPorta,
  mVibrato,
  mAddVol,
  mSetVol,
  mEnv,
  mLoopUp,
  mAddBegin,
  mAddLen,
  mOff,
  mWaitUp,
  mGoSub,
  mRet,
  mSetPeriod,
  mSetLoop,
  mOneShot,
  mWaitSample,
  mRand,
  mSKey,
  mSVol,
  mAddVolNote,
  mSetPrevNote,
  mSignal,
  mPlayMacro
};

class TFMXPlayer {
  struct TFMXChan {
    int pos;
    int apos;
    int seek;
    unsigned short len;
    unsigned short loop;
    unsigned short freq;
    char vol;
    TFMXChan(): pos(0), seek(0), len(0), freq(0), vol(0) {}
  } chan[8]; 
  char* smpl;
  size_t smplLen;
  TFMXHeader head;
  TFMXOrders track[128][8];
  TFMXPatData pat[128][256];
  TFMXMacroData macro[128][256];
  int patPoint[128];
  int macroPoint[128];
  int ciaVal, ciaCount;
  
  struct {
    int index;
    int pos;
    int tim;
    int vol;
    int note;
  } cstat[8];
  
  struct {
    int index;
    int pos;
    int tim;
    int trans;
  } tstat[8];
  int curSong, curRow, curTick, speed;
  
  bool updateTrack(int tr);
  void updateRow(int row);
  void nextTick();
  public:
    void nextSample(short* l, short* r);
    int play(int song);
    int stop();
    bool load(const char* mdat, const char* smpl);
    void playMacro(char macro, char note, char vol, unsigned char c, int trans);
    TFMXPlayer(): ciaVal(59659) {}
};
