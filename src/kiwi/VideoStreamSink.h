#ifndef VIDEOSTREAMSINK_H
#define VIDEOSTREAMSINK_H
#include <opencv/cv.h>
#include <opencv/highgui.h>
#include "VideoStreamDecoder.h"
#include <QThread>
#include <QTimer>

class VideoStreamSink: public QThread {
public:
	VideoStreamSink();
	virtual ~VideoStreamSink();
	IplImage *getNextFrame();

private:
	VideoStreamDecoder *decoder;
}

#endif
