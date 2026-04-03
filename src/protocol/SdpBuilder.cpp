#include "protocol/SdpBuilder.h"
#include <string>

std::string SdpBuilder::Build(
    const std::string& clientIP,
    const std::string& receiverIP,
    uint64_t           sessionId,
    const std::string& rsaAesKey_b64,
    const std::string& aesIv_b64,
    bool               useEncryption)
{
    std::string sdp;
    sdp.reserve(512);

    sdp += "v=0\r\n";

    sdp += "o=iTunes ";
    sdp += std::to_string(sessionId);
    sdp += " 0 IN IP4 ";
    sdp += clientIP;
    sdp += "\r\n";

    sdp += "s=iTunes\r\n";

    sdp += "c=IN IP4 ";
    sdp += clientIP;
    sdp += "\r\n";

    sdp += "t=0 0\r\n";
    sdp += "m=audio 0 RTP/AVP 96\r\n";
    sdp += "a=rtpmap:96 AppleLossless/44100/2\r\n";

    // fmtp parameters matching shairport-sync / reference RAOP senders:
    // frameLength=352, compatibleVersion=0, bitDepth=16, rice1=40, rice2=10,
    // byteStreamVersion=14, channels=2, maxRun=255, maxFrameBytes=0, sampleRate=44100
    sdp += "a=fmtp:96 352 0 16 40 10 14 2 255 0 44100\r\n";

    if (useEncryption) {
        sdp += "a=rsaaeskey:";
        sdp += rsaAesKey_b64;
        sdp += "\r\n";

        sdp += "a=aesiv:";
        sdp += aesIv_b64;
        sdp += "\r\n";
    }

    return sdp;
}

