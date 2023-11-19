#include "ffvartsp.hh"

char eventLoopWatchVariable = 0;
const size_t MAX_BUFFER_SIZE = 10;

UsageEnvironment& operator<<(UsageEnvironment& env,
                             const RTSPClient& rtspClient) {
  return env << "[URL:\"" << rtspClient.url() << "\"]: ";
}

UsageEnvironment& operator<<(UsageEnvironment& env,
                             const MediaSubsession& subsession) {
  return env << subsession.mediumName() << "/" << subsession.codecName();
}

int ffvartsp_init(const char* app_name,
                  const char* rtsp_url,
                  std::queue<std::vector<uint8_t>>& rtsp_packet_queue,
                  std::mutex& rtsp_packet_queue_mutex,
                  std::condition_variable& rtsp_packet_queue_cv) {
  // Begin by setting up our usage environment:
  TaskScheduler* scheduler = BasicTaskScheduler::createNew();
  UsageEnvironment* env = BasicUsageEnvironment::createNew(*scheduler);

  // There are argc-1 URLs: argv[1] through argv[argc-1].  Open and start
  // streaming each one:

  openURL(*env, app_name, rtsp_url, rtsp_packet_queue, rtsp_packet_queue_mutex,
          rtsp_packet_queue_cv);

  // All subsequent activity takes place within the event loop:
  env->taskScheduler().doEventLoop(&eventLoopWatchVariable);
  // This function call does not return, unless, at some point in time,
  // "eventLoopWatchVariable" gets set to something non-zero.

  return 0;

  // If you choose to continue the application past this point (i.e., if you
  // comment out the "return 0;" statement above), and if you don't intend to do
  // anything more with the "TaskScheduler" and "UsageEnvironment" objects, then
  // you can also reclaim the (small) memory used by these objects by
  // uncommenting the following code:
  /*
    env->reclaim(); env = NULL;
    delete scheduler; scheduler = NULL;
  */
}

#define RTSP_CLIENT_VERBOSITY_LEVEL \
  0  // by default, print verbose output from each "RTSPClient"

static unsigned rtspClientCount =
    0;  // Counts how many streams (i.e., "RTSPClient"s) are currently in use.

void openURL(UsageEnvironment& env,
             const char* progName,
             const char* rtspURL,
             std::queue<std::vector<uint8_t>>& rtsp_packet_queue,
             std::mutex& rtsp_packet_queue_mutex,
             std::condition_variable& rtsp_packet_queue_cv) {
  // Begin by creating a "RTSPClient" object.  Note that there is a separate
  // "RTSPClient" object for each stream that we wish to receive (even if more
  // than stream uses the same "rtsp://" URL).
  RTSPClient* rtspClient = ourRTSPClient::createNew(
      env, rtspURL, rtsp_packet_queue, rtsp_packet_queue_mutex,
      rtsp_packet_queue_cv, RTSP_CLIENT_VERBOSITY_LEVEL, progName);
  if (rtspClient == NULL) {
    env << "Failed to create a RTSP client for URL \"" << rtspURL
        << "\": " << env.getResultMsg() << "\n";
    return;
  }

  ++rtspClientCount;

  // Next, send a RTSP "DESCRIBE" command, to get a SDP description for the
  // stream. Note that this command - like all RTSP commands - is sent
  // asynchronously; we do not block, waiting for a response. Instead, the
  // following function call returns immediately, and we handle the RTSP
  // response later, from within the event loop:
  rtspClient->sendDescribeCommand(continueAfterDESCRIBE);
}

// Implementation of the RTSP 'response handlers':

void continueAfterDESCRIBE(RTSPClient* rtspClient,
                           int resultCode,
                           char* resultString) {
  do {
    UsageEnvironment& env = rtspClient->envir();                 // alias
    StreamClientState& scs = ((ourRTSPClient*)rtspClient)->scs;  // alias

    if (resultCode != 0) {
      env << *rtspClient << "Failed to get a SDP description: " << resultString
          << "\n";
      delete[] resultString;
      break;
    }

    char* const sdpDescription = resultString;
    env << *rtspClient << "Got a SDP description:\n" << sdpDescription << "\n";

    // Create a media session object from this SDP description:
    scs.session = MediaSession::createNew(env, sdpDescription);
    delete[] sdpDescription;  // because we don't need it anymore
    if (scs.session == NULL) {
      env << *rtspClient
          << "Failed to create a MediaSession object from the SDP description: "
          << env.getResultMsg() << "\n";
      break;
    } else if (!scs.session->hasSubsessions()) {
      env << *rtspClient
          << "This session has no media subsessions (i.e., no \"m=\" lines)\n";
      break;
    }

    // Then, create and set up our data source objects for the session.  We do
    // this by iterating over the session's 'subsessions', calling
    // "MediaSubsession::initiate()", and then sending a RTSP "SETUP" command,
    // on each one. (Each 'subsession' will have its own data source.)
    scs.iter = new MediaSubsessionIterator(*scs.session);
    setupNextSubsession(rtspClient);
    return;
  } while (0);

  // An unrecoverable error occurred with this stream.
  shutdownStream(rtspClient);
}

// By default, we request that the server stream its data using RTP/UDP.
// If, instead, you want to request that the server stream via RTP-over-TCP,
// change the following to True:
#define REQUEST_STREAMING_OVER_TCP False

void setupNextSubsession(RTSPClient* rtspClient) {
  UsageEnvironment& env = rtspClient->envir();                 // alias
  StreamClientState& scs = ((ourRTSPClient*)rtspClient)->scs;  // alias

  scs.subsession = scs.iter->next();
  if (scs.subsession != NULL) {
    if (!scs.subsession->initiate()) {
      env << *rtspClient << "Failed to initiate the \"" << *scs.subsession
          << "\" subsession: " << env.getResultMsg() << "\n";
      setupNextSubsession(
          rtspClient);  // give up on this subsession; go to the next one
    } else {
      //   env << *rtspClient << "Initiated the \"" << *scs.subsession
      //       << "\" subsession (";
      if (scs.subsession->rtcpIsMuxed()) {
        // env << "client port " << scs.subsession->clientPortNum();
      } else {
        // env << "client ports " << scs.subsession->clientPortNum() << "-"
        //     << scs.subsession->clientPortNum() + 1;
      }
      //   env << ")\n";

      // Continue setting up this subsession, by sending a RTSP "SETUP" command:
      rtspClient->sendSetupCommand(*scs.subsession, continueAfterSETUP, False,
                                   REQUEST_STREAMING_OVER_TCP);
    }
    return;
  }

  // We've finished setting up all of the subsessions.  Now, send a RTSP "PLAY"
  // command to start the streaming:
  if (scs.session->absStartTime() != NULL) {
    // Special case: The stream is indexed by 'absolute' time, so send an
    // appropriate "PLAY" command:
    rtspClient->sendPlayCommand(*scs.session, continueAfterPLAY,
                                scs.session->absStartTime(),
                                scs.session->absEndTime());
  } else {
    scs.duration = scs.session->playEndTime() - scs.session->playStartTime();
    rtspClient->sendPlayCommand(*scs.session, continueAfterPLAY);
  }
}

void continueAfterSETUP(RTSPClient* rtspClient,
                        int resultCode,
                        char* resultString) {
  do {
    UsageEnvironment& env = rtspClient->envir();                 // alias
    StreamClientState& scs = ((ourRTSPClient*)rtspClient)->scs;  // alias

    if (resultCode != 0) {
      env << *rtspClient << "Failed to set up the \"" << *scs.subsession
          << "\" subsession: " << resultString << "\n";
      break;
    }

    // env << *rtspClient << "Set up the \"" << *scs.subsession
    //     << "\" subsession (";
    if (scs.subsession->rtcpIsMuxed()) {
      //   env << "client port " << scs.subsession->clientPortNum();
    } else {
      //   env << "client ports " << scs.subsession->clientPortNum() << "-"
      //       << scs.subsession->clientPortNum() + 1;
    }
    // env << ")\n";

    // Having successfully setup the subsession, create a data sink for it, and
    // call "startPlaying()" on it. (This will prepare the data sink to receive
    // data; the actual flow of data from the client won't start happening until
    // later, after we've sent a RTSP "PLAY" command.)

    scs.subsession->sink = DummySink::createNew(env, *scs.subsession,
                                                rtspClient->url(), rtspClient);
    // perhaps use your own custom "MediaSink" subclass instead
    if (scs.subsession->sink == NULL) {
      env << *rtspClient << "Failed to create a data sink for the \""
          << *scs.subsession << "\" subsession: " << env.getResultMsg() << "\n";
      break;
    }

    // env << *rtspClient << "Created a data sink for the \"" << *scs.subsession
    //     << "\" subsession\n";
    scs.subsession->miscPtr =
        rtspClient;  // a hack to let subsession handler functions get the
                     // "RTSPClient" from the subsession
    scs.subsession->sink->startPlaying(*(scs.subsession->readSource()),
                                       subsessionAfterPlaying, scs.subsession);
    // Also set a handler to be called if a RTCP "BYE" arrives for this
    // subsession:
    if (scs.subsession->rtcpInstance() != NULL) {
      scs.subsession->rtcpInstance()->setByeWithReasonHandler(
          subsessionByeHandler, scs.subsession);
    }
  } while (0);
  delete[] resultString;

  // Set up the next subsession, if any:
  setupNextSubsession(rtspClient);
}

void continueAfterPLAY(RTSPClient* rtspClient,
                       int resultCode,
                       char* resultString) {
  Boolean success = False;

  do {
    UsageEnvironment& env = rtspClient->envir();                 // alias
    StreamClientState& scs = ((ourRTSPClient*)rtspClient)->scs;  // alias

    if (resultCode != 0) {
      env << *rtspClient << "Failed to start playing session: " << resultString
          << "\n";
      break;
    }

    // Set a timer to be handled at the end of the stream's expected duration
    // (if the stream does not already signal its end using a RTCP "BYE").  This
    // is optional.  If, instead, you want to keep the stream active - e.g., so
    // you can later 'seek' back within it and do another RTSP "PLAY" - then you
    // can omit this code. (Alternatively, if you don't want to receive the
    // entire stream, you could set this timer for some shorter value.)
    if (scs.duration > 0) {
      unsigned const delaySlop =
          2;  // number of seconds extra to delay, after the stream's expected
              // duration.  (This is optional.)
      scs.duration += delaySlop;
      unsigned uSecsToDelay = (unsigned)(scs.duration * 1000000);
      scs.streamTimerTask = env.taskScheduler().scheduleDelayedTask(
          uSecsToDelay, (TaskFunc*)streamTimerHandler, rtspClient);
    }

    env << *rtspClient << "Started playing session";
    if (scs.duration > 0) {
      env << " (for up to " << scs.duration << " seconds)";
    }
    env << "...\n";

    success = True;
  } while (0);
  delete[] resultString;

  if (!success) {
    // An unrecoverable error occurred with this stream.
    shutdownStream(rtspClient);
  }
}

// Implementation of the other event handlers:

void subsessionAfterPlaying(void* clientData) {
  MediaSubsession* subsession = (MediaSubsession*)clientData;
  RTSPClient* rtspClient = (RTSPClient*)(subsession->miscPtr);

  // Begin by closing this subsession's stream:
  Medium::close(subsession->sink);
  subsession->sink = NULL;

  // Next, check whether *all* subsessions' streams have now been closed:
  MediaSession& session = subsession->parentSession();
  MediaSubsessionIterator iter(session);
  while ((subsession = iter.next()) != NULL) {
    if (subsession->sink != NULL)
      return;  // this subsession is still active
  }

  // All subsessions' streams have now been closed, so shutdown the client:
  shutdownStream(rtspClient);
}

void subsessionByeHandler(void* clientData, char const* reason) {
  MediaSubsession* subsession = (MediaSubsession*)clientData;
  RTSPClient* rtspClient = (RTSPClient*)subsession->miscPtr;
  UsageEnvironment& env = rtspClient->envir();  // alias

  env << *rtspClient << "Received RTCP \"BYE\"";
  if (reason != NULL) {
    env << " (reason:\"" << reason << "\")";
    delete[] (char*)reason;
  }
  env << " on \"" << *subsession << "\" subsession\n";

  // Now act as if the subsession had closed:
  subsessionAfterPlaying(subsession);
}

void streamTimerHandler(void* clientData) {
  ourRTSPClient* rtspClient = (ourRTSPClient*)clientData;
  StreamClientState& scs = rtspClient->scs;  // alias

  scs.streamTimerTask = NULL;

  // Shut down the stream:
  shutdownStream(rtspClient);
}

void shutdownStream(RTSPClient* rtspClient, int exitCode) {
  UsageEnvironment& env = rtspClient->envir();                 // alias
  StreamClientState& scs = ((ourRTSPClient*)rtspClient)->scs;  // alias

  // First, check whether any subsessions have still to be closed:
  if (scs.session != NULL) {
    Boolean someSubsessionsWereActive = False;
    MediaSubsessionIterator iter(*scs.session);
    MediaSubsession* subsession;

    while ((subsession = iter.next()) != NULL) {
      if (subsession->sink != NULL) {
        Medium::close(subsession->sink);
        subsession->sink = NULL;

        if (subsession->rtcpInstance() != NULL) {
          subsession->rtcpInstance()->setByeHandler(
              NULL, NULL);  // in case the server sends a RTCP "BYE" while
                            // handling "TEARDOWN"
        }

        someSubsessionsWereActive = True;
      }
    }

    if (someSubsessionsWereActive) {
      // Send a RTSP "TEARDOWN" command, to tell the server to shutdown the
      // stream. Don't bother handling the response to the "TEARDOWN".
      rtspClient->sendTeardownCommand(*scs.session, NULL);
    }
  }

  env << *rtspClient << "Closing the stream.\n";
  Medium::close(rtspClient);
  // Note that this will also cause this stream's "StreamClientState" structure
  // to get reclaimed.

  if (--rtspClientCount == 0) {
    // The final stream has ended, so exit the application now.
    // (Of course, if you're embedding this code into your own application, you
    // might want to comment this out, and replace it with
    // "eventLoopWatchVariable = 1;", so that we leave the LIVE555 event loop,
    // and continue running "main()".)
    exit(exitCode);
  }
}

// Implementation of "ourRTSPClient":

ourRTSPClient* ourRTSPClient::createNew(
    UsageEnvironment& env,
    char const* rtspURL,
    std::queue<std::vector<uint8_t>>& rtsp_packet_queue,
    std::mutex& rtsp_packet_queue_mutex,
    std::condition_variable& rtsp_packet_queue_cv,
    int verbosityLevel,
    char const* applicationName,
    portNumBits tunnelOverHTTPPortNum) {
  return new ourRTSPClient(env, rtspURL, rtsp_packet_queue,
                           rtsp_packet_queue_mutex, rtsp_packet_queue_cv,
                           verbosityLevel, applicationName,
                           tunnelOverHTTPPortNum);
}

ourRTSPClient::ourRTSPClient(
    UsageEnvironment& env,
    char const* rtspURL,
    std::queue<std::vector<uint8_t>>& rtsp_packet_queue_,
    std::mutex& rtsp_packet_queue_mutex_,
    std::condition_variable& rtsp_packet_queue_cv_,
    int verbosityLevel,
    char const* applicationName,
    portNumBits tunnelOverHTTPPortNum)
    : RTSPClient(env,
                 rtspURL,
                 verbosityLevel,
                 applicationName,
                 tunnelOverHTTPPortNum,
                 -1),
      rtsp_packet_queue(rtsp_packet_queue_),
      rtsp_packet_queue_mutex(rtsp_packet_queue_mutex_),
      rtsp_packet_queue_cv(rtsp_packet_queue_cv_) {}

ourRTSPClient::~ourRTSPClient() {}

// Implementation of "StreamClientState":

StreamClientState::StreamClientState()
    : iter(NULL),
      session(NULL),
      subsession(NULL),
      streamTimerTask(NULL),
      duration(0.0) {}

StreamClientState::~StreamClientState() {
  delete iter;
  if (session != NULL) {
    // We also need to delete "session", and unschedule "streamTimerTask" (if
    // set)
    UsageEnvironment& env = session->envir();  // alias

    env.taskScheduler().unscheduleDelayedTask(streamTimerTask);
    Medium::close(session);
  }
}

// Implementation of "DummySink":

// Even though we're not going to be doing anything with the incoming data, we
// still need to receive it. Define the size of the buffer that we'll use:
#define DUMMY_SINK_RECEIVE_BUFFER_SIZE 1024 * 1024

DummySink* DummySink::createNew(UsageEnvironment& env,
                                MediaSubsession& subsession,
                                char const* streamId,
                                RTSPClient* rtspClient_p) {
  return new DummySink(env, subsession, streamId, rtspClient_p);
}

DummySink::DummySink(UsageEnvironment& env,
                     MediaSubsession& subsession,
                     char const* streamId,
                     RTSPClient* rtspClient_p)
    : MediaSink(env), fSubsession(subsession), rtspClient(rtspClient_p) {
  fStreamId = strDup(streamId);
  fReceiveBuffer = new u_int8_t[DUMMY_SINK_RECEIVE_BUFFER_SIZE];
  fReceiveBufferWithHeader = new u_int8_t[DUMMY_SINK_RECEIVE_BUFFER_SIZE + 4];
}

DummySink::~DummySink() {
  delete[] fReceiveBuffer;
  delete[] fReceiveBufferWithHeader;
  delete[] fStreamId;
}

void DummySink::afterGettingFrame(void* clientData,
                                  unsigned frameSize,
                                  unsigned numTruncatedBytes,
                                  struct timeval presentationTime,
                                  unsigned durationInMicroseconds) {
  DummySink* sink = (DummySink*)clientData;
  sink->afterGettingFrame(frameSize, numTruncatedBytes, presentationTime,
                          durationInMicroseconds);
}

// If you don't want to see debugging output for each received frame, then
// comment out the following line:
#define DEBUG_PRINT_EACH_RECEIVED_FRAME 0
static bool begin_codec = false;

void DummySink::afterGettingFrame(unsigned frameSize,
                                  unsigned numTruncatedBytes,
                                  struct timeval presentationTime,
                                  unsigned /*durationInMicroseconds*/) {
  bool isVideo = (strcmp(fSubsession.mediumName(), "video") == 0);
  bool isCorrectCodec = false;
  for (auto& codec : enabled_codec) {
    if (strcmp(fSubsession.codecName(), codec.c_str()) == 0) {
      isCorrectCodec = true;
      break;
    }
  }

  if ((fReceiveBuffer[0] & 0x1F) == 7) {
    begin_codec = true;
  }

  if (!(isVideo && isCorrectCodec) || !begin_codec) {
    envir() << "Wrong medium: " << isVideo << fSubsession.mediumName()
            << ", Wrong codec: " << isCorrectCodec << fSubsession.codecName()
            << "\n";
    continuePlaying();
    return;
  }

  if (!rtspClient) {
    return;
  }
  ourRTSPClient* rtspClient_p = (ourRTSPClient*)rtspClient;

  char codec_header[4] = {00, 00, 00, 01};
  // envir() << "get frame size: " << frameSize << "\n";
  memcpy(fReceiveBufferWithHeader, codec_header, 4);
  std::vector<uint8_t> fReceiveBuffer_vec(4 + frameSize);
  memcpy(fReceiveBuffer_vec.data(), fReceiveBufferWithHeader, 4);
  memcpy(fReceiveBuffer_vec.data() + 4, fReceiveBuffer, frameSize);

  std::unique_lock<std::mutex> lck(rtspClient_p->rtsp_packet_queue_mutex);
  while (rtspClient_p->rtsp_packet_queue.size() >= MAX_BUFFER_SIZE) {
    rtspClient_p->rtsp_packet_queue.pop();
  }
  rtspClient_p->rtsp_packet_queue.push(fReceiveBuffer_vec);
  lck.unlock();
  rtspClient_p->rtsp_packet_queue_cv.notify_one();
  // We've just received a frame of data.  (Optionally) print out information
  // about it:
#if (DEBUG_PRINT_EACH_RECEIVED_FRAME)
  if (fStreamId != NULL)
    envir() << "Stream \"" << fStreamId << "\"; ";
  envir() << fSubsession.mediumName() << "/" << fSubsession.codecName()
          << ":\tReceived " << frameSize << " bytes";
  if (numTruncatedBytes > 0)
    envir() << " (with " << numTruncatedBytes << " bytes truncated)";
  char uSecsStr[6 + 1];  // used to output the 'microseconds' part of the
                         // presentation time
  sprintf(uSecsStr, "%06u", (unsigned)presentationTime.tv_usec);
  envir() << ".\tPresentation time: " << (int)presentationTime.tv_sec << "."
          << uSecsStr;
  if (fSubsession.rtpSource() != NULL &&
      !fSubsession.rtpSource()->hasBeenSynchronizedUsingRTCP()) {
    envir() << "!";  // mark the debugging output to indicate that this
                     // presentation time is not RTCP-synchronized
  }
#ifdef DEBUG_PRINT_NPT
  envir() << "\tNPT: " << fSubsession.getNormalPlayTime(presentationTime);
#endif
  envir() << "\n";
#endif

  // Then continue, to request the next frame of data:
  continuePlaying();
}

Boolean DummySink::continuePlaying() {
  if (fSource == NULL)
    return False;  // sanity check (should not happen)

  // Request the next frame of data from our input source. "afterGettingFrame()"
  // will get called later, when it arrives:
  fSource->getNextFrame(fReceiveBuffer, DUMMY_SINK_RECEIVE_BUFFER_SIZE,
                        afterGettingFrame, this, onSourceClosure, this);
  return True;
}
