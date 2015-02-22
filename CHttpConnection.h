
/**
  * Note
  *
  */

#pragma once

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <cstdio>
#include <cstdlib>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <CLocker.h>

class CHttpConnection {
	public:
		// support GET only
		enum METHOD {
			GET = 0,
			POST,
			HEAD,
			PUT,
			DELETE,
			TRACE,
			OPTIONS,
			CONNECT,
			PATCH
		};

		enum CHECK_STATE {
			CHECK_STATE_REQUESTLINE = 0,
			CHECK_STATE_HEADER,
			CHECK_STATE_CONTENT
		};

		enum HTTP_CODE {
			NO_REQUEST = 0,
			GET_REQUEST,
			BAD_REQUEST,
			NO_RESOURCE,
			FORBIDDEN_REQUEST,
			FILE_REQUEST,
			INTERNAL_ERROR,
			CLOSED_CONNECTION
		};

		enum LINE_STATUS {
			LINE_OK = 0,
			LINE_BAD,
			LINE_OPEN
		};

		CHttpConnection();
		~CHttpConnection();

		void Init(int sockfd, const sockaddr_in &addr);

		void CloseConn(bool realClose = true);

		void Process();

		bool Read();

		bool Write();

		static int m_epollfd;
		static int m_userCnt;

		static const int MAX_FILENAME = 200;
		static const int READ_BUFFER_SIZE = 2048;
		static const int WRITE_BUFFER_SIZE = 1024;

	private:
		void Init();

		HTTP_CODE ProcessRead();

		bool ProcessWrite(HTTP_CODE ret);

		// parse http request
		HTTP_CODE ParseRequestLine(char *text);
		HTTP_CODE ParseHeaders(char *text);
		HTTP_CODE ParseContent(char *text);
		HTTP_CODE DoRequest();
		LINE_STATUS ParseLine();
		char* GetLine() { return m_readBuf + m_startLine; }

		void Unmap();
		bool AddResponse(const char * format, ...);
		bool AddContent(const char *content);
		bool AddStatusLine(int status, const char *title);
		bool AddHeader(int contentLength);
		bool AddContentLength(int contentLength);
		bool AddLinger();
		bool AddBlankLine();

		int m_sockfd;
		sockaddr_in m_addr;

		int m_readIdx;
		int m_checkdIdx;
		int m_startLien;
		char m_readBuf[READ_BUFFER_SIZE];

		int m_writeIdx;
		char m_writeBuf[WRITE_BUFFER_SIZE];

		CHECK_STATE m_checkState;
		METHOD m_method;

		char m_readFile[MAX_FILENAME];
		char *m_url;
		char *m_version;
		char *m_host;
		int m_contentLength;
		bool m_linger;

		char *m_fileAddress;
		struct stat m_fileStat;
		struct iovec m_iv[2];
		int m_ivCnt;
};
