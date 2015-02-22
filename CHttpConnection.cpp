
#include "CHttpConnection.h"

const char *OK_TITILE = "OK";
const char *ERROR_400_TITLE = "Bad Request";
const char *ERROR_400_FORM = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char *ERROR_403_TITLE = "Forbidden";
const char *ERROR_403_FORM = "You do not have permission to get file from this server.\n";
const char *ERROR_404_TITLE = "Not Found";
const char *ERROR_404_FORM = "The requestd file was not found on this server.\n";
const char *ERROR_500_TITLE = "Internal Error";
const char *ERROR_500_FORM = "There was an unusual problem serving the requested file.\n";

const char *DOC_ROOT = "/var/html";

int CHttpConnection::m_userCnt = 0;
int CHttpConnection::m_epollfd = -1;

int SetNonblock(int fd) {
	int old = fcntl(fd, F_GETFL);
	fcntl(fd, F_SETFL, old | O_NONBLOCK);
	return old;
}

void AddFd(int epollfd, int fd, bool oneShot) {
	epoll_event event;
	event.data.fd = fd;
	event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
	if( oneShot ) {
		event.events |= EPOLLONESHOT;
	}
	epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
	SetNonblock(fd);
}

void RemoveFd(int epollfd, int fd) {
	epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
	close(fd);
}

void ModFd(int epollfd, int fd, int ev) {
	epoll_event event;
	event.data.fd = fd;
	event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
	epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

void CHttpConnection::CloseConn(bool realClose) {
	if( realClose && (m_sockfd != -1) ) {
		RemoveFd(m_epollfd, m_sockfd);
		m_sockfd = -1;
		--m_userCnt;
	}
}

void CHttpConnection::Init(int sockfd, const sockaddr_in &addr) {
	m_sockfd = sockfd;
	m_addr = addr;

	AddFd(m_epollfd, sockfd, true);
	++m_userCnt;
	Init();
}

void CHttpConnection::Init() {
	m_checkState = CHECK_STATE_REQUESTLINE;
	m_linger = false;

	m_method = GET;
	m_url = NULL;
	m_host = NULL;

	m_version = 0;
	m_contentLength = 0;
	m_startLine = 0;
	m_checkdIdx = 0;
	m_readIdx = 0;
	m_writeIdx = 0;

	memset(m_readBuf, 0, sizeof(m_readBuf));
	memset(m_writeBuf, 0, sizeof(m_writeBuf));
	memset(m_readFile, 0, sizeof(m_readFile));
}

// line should end in "\r\n"
CHttpConnection::LINE_STATUS CHttpConnection::ParseLine() {
	char tmp;
	for(; m_checkdIdx < m_readIdx; ++m_checkdIdx) {
		tmp = m_readBuf[m_checkdIdx];
		if( tmp == '\r' ) {
			if( (m_checkdIdx + 1) == m_readIdx ) {
				return LINE_OPEN;
			}
			else if( m_readBuf[m_checkdIdx + 1] == '\n' ) {
				m_readBuf[m_checkdIdx++] = '\0';
				m_readBuf[m_checkdIdx++] = '\0';
				return LINE_OK;
			}

			return LINE_BAD;
		}
		else if( tmp == '\n' ) {
			if( (m_checkdIdx > 1) && (m_readBuf[m_checkdIdx - 1] == '\r') ) {
				m_readBuf[m_checkdIdx++] = '\0';
				m_readBuf[m_checkdIdx++] = '\0';
				return LINE_OK;
			}
			
			return LINE_BAD;
		}
	}

	return LINE_OPEN;
}

bool CHttpConnection::Read() {
	if( m_readIdx >= READ_BUFFER_SIZE ) {
		return false;
	}

	int bytesRead = 0;
	while( true ) {
		bytesRead = recv(m_sockfd, m_readBuf + m_readIdx, READ_BUFFER_SIZE - m_readIdx, 0);
		if( bytesRead == -1 ) {
			if( errno == EAGAIN || errno == EWOULDBLOCK ) {
				break;
			}
			return false;
		}
		else if( bytesRead == 0 ) {
			return false;
		}
		
		m_readIdx += bytesRead;
	}

	return true;
}

CHttpConnection::HTTP_CODE CHttpConnection::ParseRequestLine(char *text) {
	// accept method GET only
	char *ctmp = strpbrk(text, " \t");
	if( !ctmp ) {
		printf("%d: ERR! bad request %s\n", __LINE__, text);
		return BAD_REQUEST;
	}
	*ctmp++ = '\0';
	if( strcasecmp(text, "GET") == 0 ) {
		m_method = GET;
	}
	else {
		printf("accept GET only now, reject method[%s]\n", text);
		return BAD_REQUEST;
	}

	m_url = ctmp + strspn(ctmp, "\t");
	m_version = strpbrk(m_url, " \t");
	if( !m_version ) {
		printf("%d: ERR! bad request %s\n", __LINE__, text);
		return BAD_REQUEST;
	}
	*m_version++ = '\0';
	m_version += strspn(m_version, " \t");

	if( strncasecmp(m_url, "http://", 7) == 0 ) {
		m_url += 7;
		m_url = strchr(m_url, '/');
	}

	if( !m_url || m_url[0] != '/' ) {
		printf("%d: ERR! bad request %s\n", __LINE__, text);
		return BAD_REQUEST;
	}

	m_checkState = CHECK_STATE_HEADER;
	return NO_REQUEST;
}

CHttpConnection::HTTP_CODE CHttpConnection::ParseHeaders(char *text) {
	if( text[0] == '\0' ) {
		if( m_contentLength != 0 ) {
			m_checkState = CHECK_STATE_CONTENT;
			return NO_REQUEST;
		}

		return GET_REQUEST;
	}
	else if( strncasecmp(text, "Connection:", 11) == 0 ) {
		text += 11;
		text += strspn(text, " \t");
		if( strcasecmp(text, "keep-alive") == 0 ) {
			m_linger = true;
		}
	}
	else if( strncasecmp(text, "Content-Length:", 15) == 0 ) {
		text += 15;
		text += strspn(text, " \t");
		m_contentLength = atol(text);
	}
	else if( strncasecmp(text, "Host:", 5) == 0 ) {
		text += 5;
		text += strspn(text, " \t");
		m_host = text;
	}
	else {
		printf("opp! unknow header %s\n", text);
	}

	return NO_REQUEST;
}

// not parse the contnet actually
CHttpConnection::HTTP_CODE CHttpConnection::ParseContent(char *text) {
	if( m_readIdx >= (m_contentLength + m_checkdIdx) ) {
		text[m_contentLength] = '\0';
		return GET_REQUEST;
	}

	return NO_REQUEST;
}

CHttpConnection::HTTP_CODE CHttpConnection::ProcessRead() {
	HTTP_CODE ret = NO_REQUEST;
	LINE_STATUS lineStatus = LINE_OK;
	while( ((m_checkState == CHECK_STATE_CONTENT) && (lineStatus == LINE_OK)) || ((lineStatus = ParseLine()) == LINE_OK )) {
			char *text = GetLine();
			m_startLine = m_checkdIdx;
			printf("got http line: %s\n", text);

			switch(m_checkState) {
				case CHECK_STATE_REQUESTLINE:
					if( (ret = ParseRequestLine(text)) == BAD_REQUEST ) {
						return BAD_REQUEST;
					}
					break;
				case CHECK_STATE_HEADER:
					if( (ret = ParseHeaders(text)) == BAD_REQUEST ) {
						return BAD_REQUEST;
					}
					else if( ret == GET_REQUEST ) {
						return DoRequest();
					}
					break;
				case CHECK_STATE_CONTENT:
					if( (ret = ParseContent(text)) == GET_REQUEST ) {
						return DoRequest();
					}
					lineStatus = LINE_OPEN;
					break;
				default:
					return INTERNAL_ERROR;
			}
	}

	return NO_REQUEST;
}

CHttpConnection::HTTP_CODE CHttpConnection::DoRequest() {
	int len = strlen(DOC_ROOT);
	strcpy(m_readFile, DOC_ROOT);
	strncpy(m_readFile + len, m_url, MAX_FILENAME - len - 1);
	printf("require %s\n", m_readFile);

	if( stat(m_readFile, &m_fileStat) < 0 ) {
		return NO_RESOURCE;
	}

	if( !(m_fileStat.st_mode & S_IROTH) ) {
		return FORBIDDEN_REQUEST;
	}

	if( S_ISDIR(m_fileStat.st_mode) ) {
		return BAD_REQUEST;
	}

	int fd = open(m_readFile, O_RDONLY);
	m_fileAddress = (char*)mmap(0, m_fileStat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	return FILE_REQUEST;
}

void CHttpConnection::Unmap() {
	if( m_fileAddress ) {
		munmap(m_fileAddress, m_fileStat.st_size);
		m_fileAddress = NULL;
	}
}

bool CHttpConnection::Write() {
	int bytesToSend = m_writeIdx;
	if( bytesToSend == 0 ) {
		ModFd(m_epollfd, m_sockfd, EPOLLIN);
		Init();
		return true;
	}

	int bytesHaveSend;
	while( true ) {
		int tmp = writev(m_sockfd, m_iv, m_ivCnt);
		if( tmp <= -1 ) {
			if( errno == EAGAIN ) {
				ModFd(m_epollfd, m_sockfd, EPOLLOUT);
				return true;
			}
			Unmap();
			return false;
		}

		bytesToSend -= tmp;
		bytesHaveSend += tmp;
		if( bytesToSend <= bytesHaveSend ) {
			Unmap();
			if( m_linger ) {
				Init();
				ModFd(m_epollfd, m_sockfd, EPOLLIN);
				return true;
			}
			else {
				ModFd(m_epollfd, m_sockfd, EPOLLIN);
				return false;
			}
		}
	}
}

bool CHttpConnection::AddResponse(const char *format, ...) {
	if( m_writeIdx >= WRITE_BUFFER_SIZE ) {
		return false;
	}

	va_list argList;
	va_start(argList, format);
	int len = vsnprintf(m_writeBuf + m_writeIdx, WRITE_BUFFER_SIZE - 1 - m_writeIdx, format, argList);
	if( len >= (WRITE_BUFFER_SIZE - 1 - m_writeIdx) ) {
		return false;
	}
	m_writeIdx += len;
	va_end(argList);
	return true;
}

bool CHttpConnection::AddStatusLine(int status, const char *title) {
	return AddResponse("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool CHttpConnection::AddContentLength(int contentLen) {
	return AddResponse("Content-Length: %d\r\n", contentLen);
}

bool CHttpConnection::AddLinger() {
	return AddResponse("Connection: %s\r\n", (m_linger ? "keep-alive" : "close"));
}

bool CHttpConnection::AddBlankLine() {
	return AddResponse("%s", "\r\n");
}

bool CHttpConnection::AddHeader(int contentLen) {
	AddContentLength(contentLen);
	AddLinger();
	AddBlankLine();
}

bool CHttpConnection::AddContent(const char *content) {
	return AddResponse("%s", content);
}

bool CHttpConnection::ProcessWrite(HTTP_CODE ret) {
	printf("response is[%d]\n", ret);
	switch(ret) {
		case INTERNAL_ERROR:
			AddStatusLine(500, ERROR_500_TITLE);
			AddHeader(strlen(ERROR_500_FORM));
			if( !AddContent(ERROR_500_FORM) ) {
				return false;
			}
			break;
		case BAD_REQUEST:
			AddStatusLine(400, ERROR_400_TITLE);
			AddHeader(strlen(ERROR_400_FORM));
			if( !AddContent(ERROR_400_FORM) ) {
				return false;
			}
			break;
		case NO_RESOURCE:
			AddStatusLine(404, ERROR_404_TITLE);
			AddHeader(strlen(ERROR_404_FORM));
			if( !AddContent(ERROR_404_FORM) ) {
				return false;
			}
			break;
		case FORBIDDEN_REQUEST:
			AddStatusLine(403, ERROR_403_TITLE);
			AddHeader(strlen(ERROR_403_FORM));
			if( !AddContent(ERROR_403_FORM) ) {
				return false;
			}
			break;
		case FILE_REQUEST:
			AddStatusLine(200, OK_TITILE);
			if( m_fileStat.st_size != 0 ) {
				AddHeader(m_fileStat.st_size);
				m_iv[0].iov_base = m_writeBuf;
				m_iv[0].iov_len = m_writeIdx;
				m_iv[1].iov_base = m_fileAddress;
				m_iv[1].iov_len = m_fileStat.st_size;
				m_ivCnt = 2;
				return true;
			}
			else {
				const char *okString = "<html><body>error!</body></html>";
				AddHeader(strlen(okString));
				if( !AddContent(okString) ) {
					return false;
				}
			}
		default:
			return false;
	}

	m_iv[0].iov_base = m_writeBuf;
	m_iv[0].iov_len = m_writeIdx;
	m_ivCnt = 1;
	return true;
}

void CHttpConnection::Process() {
	HTTP_CODE ret = ProcessRead();
	if( ret == NO_REQUEST ) {
		ModFd(m_epollfd, m_sockfd, EPOLLIN);
		return;
	}

	bool writeRet = ProcessWrite(ret);
	if( !writeRet ) {
		CloseConn();
	}
	ModFd(m_epollfd, m_sockfd, EPOLLOUT);
}

