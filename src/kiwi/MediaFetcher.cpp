#include "MediaFetcher.h"
#include "SourceList.h"
#define FPS 25
static MediaFetcher *gMediaFetcher;
static enum ccn_upcall_res handleMediaContent(struct ccn_closure *selfp,
											  enum ccn_upcall_kind kind,
											  struct ccn_upcall_info *info);

static enum ccn_upcall_res handlePipeMediaContent(struct ccn_closure *selfp,
											  enum ccn_upcall_kind kind,
											  struct ccn_upcall_info *info);

MediaFetcher::MediaFetcher (SourceList *sourceList) {
	gMediaFetcher = this;
	this->sourceList = sourceList;
	nh = new NdnHandler();
	staleOk = true;
	fetchTimer = new QTimer(this);
	fetchTimer->setInterval(1000 / FPS);
	connect(fetchTimer, SIGNAL(timeout()), this, SLOT(fetch()));
	fetchTimer->start();
	bRunning = true;
	start();
}

MediaFetcher::~MediaFetcher() {
	bRunning = false;
	if (isRunning())
		wait();
	if (nh != NULL) {
		delete nh;
		nh = NULL;
	}
}

void MediaFetcher::run() {
	int res = 0;
	while(res >= 0 && bRunning) {
		initStream();
		res = ccn_run(nh->h, 0);
		usleep(20);
	}
}

void MediaFetcher::initStream()
{
    QHash<QString,MediaSource *>::const_iterator it = sourceList->list.constBegin(); 
    for ( ; it != sourceList->list.constEnd(); ++it ) {
		MediaSource *ms = it.value();
        if (ms && !ms->isStreaming()) {
            struct ccn_charbuf *templ = ccn_charbuf_create();
            struct ccn_charbuf *path = ccn_charbuf_create();
            ccn_charbuf_append_tt(templ, CCN_DTAG_Interest, CCN_DTAG);
            ccn_charbuf_append_tt(templ, CCN_DTAG_Name, CCN_DTAG);
            ccn_charbuf_append_closer(templ);
            ccn_charbuf_append_tt(templ, CCN_DTAG_ChildSelector, CCN_DTAG);
            ccn_charbuf_append_tt(templ, 1, CCN_UDATA);
            ccn_charbuf_append(templ, "1", 1);	/* low bit 1: rightmost */
            ccn_charbuf_append_closer(templ); /*<ChildSelector>*/
            ccn_charbuf_append_closer(templ);
			QString fullName = ms->getPrefix() + ms->getUsername();
			ccn_name_from_uri(path, fullName.toLocal8Bit().constData());
			ccn_name_append_str(path, "video");
			if (ms->fetchClosure->p == NULL) 
				ms->fetchClosure->p = &handleMediaContent;
			int res = ccn_express_interest(nh->h, path, ms->fetchClosure, templ);
			if (res < 0) {
				fprintf(stderr, "Express short interest failed\n");
			}
			ms->setStreaming(true);
            ccn_charbuf_destroy(&path);
            ccn_charbuf_destroy(&templ);
        }
    }
}

void MediaFetcher::fetch() {
	QHash<QString, MediaSource *>::const_iterator it = sourceList->list.constBegin(); 	
	while (it != sourceList->list.constEnd()) {
		QString userName = it.key();
		MediaSource *ms = it.value();
		if (ms != NULL && ms->isStreaming()) {
			ms->incSeq();
			struct ccn_charbuf *pathbuf = ccn_charbuf_create();
			QString fullName = ms->getPrefix() + ms->getUsername();
			ccn_name_from_uri(pathbuf, fullName.toLocal8Bit().constData());
			ccn_name_append_str(pathbuf, "video");
			struct ccn_charbuf *temp = ccn_charbuf_create();
			ccn_charbuf_putf(temp, "%ld", ms->getSeq());
			ccn_name_append(pathbuf, temp->buf, temp->length);
			if (ms->fetchPipelineClosure->p == NULL)
				ms->fetchPipelineClosure->p = &handlePipeMediaContent;
			int res = ccn_express_interest(nh->h, pathbuf, ms->fetchPipelineClosure, NULL);
			if (res < 0) {
				fprintf(stderr, "Sending interest failed at normal processor\n");
			}
			ccn_charbuf_destroy(&pathbuf);
			ccn_charbuf_destroy(&temp);
		}
		it++;	
	}
}


void MediaFetcher::initPipe(struct ccn_closure *selfp, struct ccn_upcall_info *info) {
	MediaSource *ms = (MediaSource *)selfp->data;
	if (!ms)
		return;

	if (ms->isStreaming())
		return;

	const unsigned char *ccnb = info->content_ccnb;
	size_t ccnb_size = info->pco->offset[CCN_PCO_E];
	struct ccn_indexbuf *comps = info->content_comps;

	long seq;
	const unsigned char *seqptr = NULL;
	char *endptr = NULL;
	size_t seq_size = 0;
	int k = comps->n - 2;


	seq = ccn_ref_tagged_BLOB(CCN_DTAG_Component, ccnb,
			comps->buf[k], comps->buf[k + 1],
			&seqptr, &seq_size);
	if (seq >= 0) {
		seq = strtol((const char *)seqptr, &endptr, 10);
		if (endptr != ((const char *)seqptr) + seq_size)
			seq = -1;
	}
	if (seq >= 0) {
		ms->setSeq(seq);
		ms->setStreaming(true);
	}
	else {
		return;
	}
	
	// send hint-ahead interests
	for (int i = 0; i < 10; i ++) {
		ms->incSeq();
		struct ccn_charbuf *pathbuf = ccn_charbuf_create();
		ccn_name_init(pathbuf);
		ccn_name_append_components(pathbuf, ccnb, comps->buf[0], comps->buf[k]);
		struct ccn_charbuf *temp = ccn_charbuf_create();
		ccn_charbuf_putf(temp, "%ld", ms->getSeq());
		ccn_name_append(pathbuf, temp->buf, temp->length);
		if (ms->fetchPipelineClosure->p == NULL)
			ms->fetchPipelineClosure->p = &handlePipeMediaContent;
		int res = ccn_express_interest(info->h, pathbuf, ms->fetchPipelineClosure, NULL);
		if (res < 0) {
			fprintf(stderr, "Sending interest failed at normal processor\n");
		}
		ccn_charbuf_destroy(&pathbuf);
		ccn_charbuf_destroy(&temp);
	}
}

void MediaFetcher::processContent(struct ccn_closure *selfp, struct ccn_upcall_info *info) {
	MediaSource *ms = (MediaSource *)selfp->data;

	const unsigned char *content = NULL;
	size_t len = 0;

	ccn_content_get_value(info->content_ccnb, info->pco->offset[CCN_PCO_E], info->pco, &content, &len);
	QString username = ms->getUsername();
	emit contentArrived(username, content, len);
}

enum ccn_upcall_res handleMediaContent(struct ccn_closure *selfp,
											  enum ccn_upcall_kind kind,
											  struct ccn_upcall_info *info) {
    MediaSource *ms  = (MediaSource *)selfp->data;
    if (ms == NULL) {
        return CCN_UPCALL_RESULT_OK;
    }

	switch (kind) {
	case CCN_UPCALL_INTEREST_TIMED_OUT: {
		return (CCN_UPCALL_RESULT_REEXPRESS);
	}
	case CCN_UPCALL_CONTENT_UNVERIFIED:
		fprintf(stderr, "unverified content received\n");
		return CCN_UPCALL_RESULT_OK;
	case CCN_UPCALL_CONTENT:
		break;
	default:
		return CCN_UPCALL_RESULT_OK;
	}

	// got some data, reset consecutiveTimeouts
	ms->resetTimeouts();

	gMediaFetcher->processContent(selfp, info);

	gMediaFetcher->initPipe(selfp, info);

    return CCN_UPCALL_RESULT_OK;
}

enum ccn_upcall_res handlePipeMediaContent(struct ccn_closure *selfp,
											  enum ccn_upcall_kind kind,
											  struct ccn_upcall_info *info) {
    MediaSource *ms  = (MediaSource *)selfp->data;
    if (ms == NULL) {
        return CCN_UPCALL_RESULT_OK;
    }
	switch (kind) {
	case CCN_UPCALL_INTEREST_TIMED_OUT: {
		ms->incTimeouts();
		// too many consecutive timeouts
		// the other end maybe crashed or stopped generating video
		if (ms->getTimeouts() > 10 && ms->isStreaming()) {
			// reset seq for this party
			ms->setSeq(0);
			ms->setStreaming(false);
		}
		return (CCN_UPCALL_RESULT_OK);
	}
	case CCN_UPCALL_CONTENT_UNVERIFIED:
		fprintf(stderr, "unverified content received\n");
		return CCN_UPCALL_RESULT_OK;
	case CCN_UPCALL_CONTENT:
		break;
	default:
		return CCN_UPCALL_RESULT_OK;

	}
	// got some data, reset consecutiveTimeouts
	ms->resetTimeouts();

	gMediaFetcher->processContent(selfp, info);

    return CCN_UPCALL_RESULT_OK;
}
