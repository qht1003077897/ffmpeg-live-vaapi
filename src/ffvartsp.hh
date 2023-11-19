
#ifndef FFVA_RTSP_H
#define FFVA_RTSP_H

#include <BasicUsageEnvironment.hh>
#include <liveMedia.hh>
#include <vector>
#include <string>

static const std::vector<std::string> enabled_codec = {"H264", "H265"};

// Forward function definitions:

// RTSP 'response handlers':
void continueAfterDESCRIBE(RTSPClient* rtspClient,
                           int resultCode,
                           char* resultString);
void continueAfterSETUP(RTSPClient* rtspClient,
                        int resultCode,
                        char* resultString);
void continueAfterPLAY(RTSPClient* rtspClient,
                       int resultCode,
                       char* resultString);

// Other event handler functions:
void subsessionAfterPlaying(
    void* clientData);  // called when a stream's subsession (e.g., audio or
                        // video substream) ends
void subsessionByeHandler(void* clientData, char const* reason);
// called when a RTCP "BYE" is received for a subsession
void streamTimerHandler(void* clientData);
// called at the end of a stream's expected duration (if the stream has not
// already signaled its end using a RTCP "BYE")

// The main streaming routine (for each "rtsp://" URL):
void openURL(UsageEnvironment& env, const char* progName, const char* rtspURL);

// Used to iterate through each stream's 'subsessions', setting up each one:
void setupNextSubsession(RTSPClient* rtspClient);

// Used to shut down and close a stream (including its "RTSPClient" object):
void shutdownStream(RTSPClient* rtspClient, int exitCode = 1);

// A function that outputs a string that identifies each stream (for debugging
// output).  Modify this if you wish:
UsageEnvironment& operator<<(UsageEnvironment& env,
                             const RTSPClient& rtspClient);
// A function that outputs a string that identifies each subsession (for
// debugging output).  Modify this if you wish:
UsageEnvironment& operator<<(UsageEnvironment& env,
                             const MediaSubsession& subsession);

extern char eventLoopWatchVariable;

int ffvartsp_init(const char* app_name, const char* rtsp_url);
// Define a class to hold per-stream state that we maintain throughout each
// stream's lifetime:

class StreamClientState {
 public:
  StreamClientState();
  virtual ~StreamClientState();

 public:
  MediaSubsessionIterator* iter;
  MediaSession* session;
  MediaSubsession* subsession;
  TaskToken streamTimerTask;
  double duration;
};

// If you're streaming just a single stream (i.e., just from a single URL,
// once), then you can define and use just a single "StreamClientState"
// structure, as a global variable in your application.  However, because - in
// this demo application - we're showing how to play multiple streams,
// concurrently, we can't do that.  Instead, we have to have a separate
// "StreamClientState" structure for each "RTSPClient".  To do this, we subclass
// "RTSPClient", and add a "StreamClientState" field to the subclass:

class ourRTSPClient : public RTSPClient {
 public:
  static ourRTSPClient* createNew(UsageEnvironment& env,
                                  char const* rtspURL,
                                  int verbosityLevel = 0,
                                  char const* applicationName = NULL,
                                  portNumBits tunnelOverHTTPPortNum = 0);

 protected:
  ourRTSPClient(UsageEnvironment& env,
                char const* rtspURL,
                int verbosityLevel,
                char const* applicationName,
                portNumBits tunnelOverHTTPPortNum);
  // called only by createNew();
  virtual ~ourRTSPClient();

 public:
  StreamClientState scs;
};

// Define a data sink (a subclass of "MediaSink") to receive the data for each
// subsession (i.e., each audio or video 'substream'). In practice, this might
// be a class (or a chain of classes) that decodes and then renders the incoming
// audio or video. Or it might be a "FileSink", for outputting the received data
// into a file (as is done by the "openRTSP" application). In this example code,
// however, we define a simple 'dummy' sink that receives incoming data, but
// does nothing with it.

class DummySink : public MediaSink {
 public:
  static DummySink* createNew(
      UsageEnvironment& env,
      MediaSubsession&
          subsession,  // identifies the kind of data that's being received
      char const* streamId = NULL);  // identifies the stream itself (optional)

 private:
  DummySink(UsageEnvironment& env,
            MediaSubsession& subsession,
            char const* streamId);
  // called only by "createNew()"
  virtual ~DummySink();

  static void afterGettingFrame(void* clientData,
                                unsigned frameSize,
                                unsigned numTruncatedBytes,
                                struct timeval presentationTime,
                                unsigned durationInMicroseconds);
  void afterGettingFrame(unsigned frameSize,
                         unsigned numTruncatedBytes,
                         struct timeval presentationTime,
                         unsigned durationInMicroseconds);

 private:
  // redefined virtual functions:
  virtual Boolean continuePlaying();

 private:
  u_int8_t* fReceiveBuffer;
  MediaSubsession& fSubsession;
  char* fStreamId;
  u_int8_t* fReceiveBufferWithHeader;
};
#endif