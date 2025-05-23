# 소스코드 수정
webserver.c 의 WEB_ROOT 매크로 변수를 rpi_server가 위치할 경로로 수정해주세요.

# 빌드 방법
make all

# 실행방법
1. index.html과 script.js를 rpi_server와 같은 위치(WEB_ROOT)에 있어야 합니다.
2. 서버 실행 : ./activate_server.sh
3. 클라이언트 실행 : ./client {서버의 ip주소}
4. sudo journalctl -f -t rpi_server -n 0 명령어로 서버의 로그 확인 가능