#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <arpa/inet.h>

#define HLE

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
    unsigned int len;
    unsigned short freq;
    signed char vol;
    bool on;
    bool looping;
    TFMXChan(): pos(0), apos(0), seek(0), len(0), freq(0), vol(0), on(false), looping(false) {}
  } chan[8]; 
  signed char* smpl;
  size_t smplLen;
  TFMXHeader head;
  TFMXOrders track[128][8];
  TFMXPatData pat[128][256];
  TFMXMacroData macro[128][256];
  int patPoint[128];
  int macroPoint[128];
  int ciaVal, ciaCount;
  
  struct TFMXCStat {
    int index;
    int pos;
    int tim;
    int vol;
    int note;
    int oldnote;
    int waitingDMA;
    bool waitingKeyUp;
    bool keyon;
    bool offReset;

    int addBegin;
    int addBeginC;
    int addBeginAmt;
    bool addBeginDir;

    int vibTime;
    int vibTimeC;
    int vibAmt;
    bool vibDir;

    int envTarget;
    int envAmt;
    int envTime;
    int envTimeC;
    bool envActive;
    
    int portaTarget;
    
    int postDMAPos;
    int postDMALen;

    short freq, detune;
    
    TFMXCStat(): index(-1), pos(0), tim(0), vol(0), note(0), oldnote(0), waitingDMA(false), waitingKeyUp(false), keyon(false), offReset(false), addBegin(0), addBeginC(0), addBeginAmt(0), addBeginDir(false), vibTime(0), vibTimeC(0), vibAmt(0), vibDir(false), envTarget(0), envAmt(0), envTime(0), envActive(false), postDMAPos(-1), postDMALen(1), freq(0), detune(0) {}
  } cstat[8];
  
  struct {
    int index;
    int pos;
    int tim;
    int trans;
  } tstat[8];
  int curSong, curRow, curTick, speed;

  float fractAccum;
  int intAccum;
  
  bool updateTrack(int tr);
  void updateRow(int row);
  void nextTick();
  void reset(int i);
  void runMacro(int i);
  void handleLoop(int c);
  public:
    float hleRate;
    void nextSample(short* l, short* r);
    void setCIAVal(int val);
    int play(int song);
    int stop();
    bool load(const char* mdat, const char* smpl);
    void playMacro(signed char macro, signed char note, signed char vol, unsigned char c, int trans);
    TFMXPlayer(): ciaVal(59659), fractAccum(0), intAccum(0), hleRate(1) {}
};
