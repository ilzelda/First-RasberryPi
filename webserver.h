void *http_server(void *arg);
void *clnt_connection(void *arg);
void sendOk(FILE* fp);
int sendData(FILE* fp, const char *ct, const char *filename);
void sendError(FILE* fp);
const char* get_mime(const char *fname);
