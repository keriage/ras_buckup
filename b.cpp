// コマンド
// g++ -Wall rescon_yaramaika.cpp -std=c++11 -lopencv_core -lopencv_highgui -lopencv_imgcodecs -lopencv_videoio -lopencv_imgproc -lpigpio -lpthread -g -O0 -o test
// sudo ./test

//-------------------------------------------------------------------------
//各種引数

#define BPS 9600                        //uartする相手と合わせること

char raspi_ip[] = "192.168.23.112";     //ラズパイ
char pc_ip[] = "192.168.23.1";          //通信先PC

int port_ras = 9001;                    //ラズパイ受信用
int port_pc_cam1 = 8081;                //サブカメラ1
int port_pc_cam2 = 8082;                //サブカメラ2
int port_pc_cam3 = 8083;                //サブカメラ3

//---------------------------------------------------------------------------

// udp
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <string>
#include <iostream>

// serial
#include <pigpio.h>

// cv
#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/imgproc.hpp>

// その他
#include <vector>
#include <thread>

using namespace std;
using namespace cv;

//カメラ送信スレッド　
// ip：通信先IP  port：ポート　　WIDTH：横幅　　HEIGHT：縦幅　　num：カメラ番号　　ratio：圧縮率
void thread_cv(int port, int WIDTH, int HEIGHT, int num, int ratio);



int main(int argc, char** argv)
{
    //カメラ用スレッド開始
    thread th1(thread_cv, port_pc_cam1, 320, 240, 0, 40);
    th1.detach();

    thread th2(thread_cv, port_pc_cam2, 320, 240, 2, 40);
    th2.detach();

    //thread th3(thread_cv, port_pc_cam2, 640 / 2, 480 / 2, 4, 20);
    //th3.detach();


    //lsusb
    //ls /dev/video*
    // シリアル通信初期設定
    if (gpioInitialise() < 0)
    {
        cout << "gpio initialization Failed1\n";
        return -1;
    }

    char* serialPortName = const_cast<char*>("/dev/serial0");       //シリアル通信の種類に注意("/dev/serial0"を確認せよ)
    int uart_handle = serOpen(serialPortName, BPS, 0);              //第三引数によって送信する文字が異なることに注意

    if (uart_handle < 0)
    {
        cout << "UART initializaition Failed2\n";                      //このエラーが出ると、aoutstartしてる可能性大
        return -1;
    }




    // UDP受信設定
    int sock;
    struct sockaddr_in server;
    char recvbuf[16];

    // 初期化
    memset(recvbuf, 0, sizeof(recvbuf));

    // アドレス情報設定
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr("0.0.0.0");
    server.sin_port = htons(port_ras);

    // ソケット生成
    sock = socket(AF_INET, SOCK_DGRAM, 0);




    // bind
    bind(sock, (struct sockaddr*)&server, sizeof(server));

    // ソケットに受信タイムアウトを設定
    struct timeval timeout;
    timeout.tv_sec = 1; // 5秒のタイムアウト
    timeout.tv_usec = 0;

    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout)) < 0) {
        perror("setsockopt");
        return -1;
    }

    //メイン部分
    //この中に処理を入れすぎると止まる可能性が大きくなった？
    while (1)
    {
        int recv_len = recv(sock, recvbuf, sizeof(recvbuf), 0);
        if (recv_len < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                cout << "No data available yet.\n";
            } else if (errno == EWOULDBLOCK) {
                cout << "Receive timed out.\n";
            } else {
                perror("recv");
            }

            cout << serWrite(uart_handle, "k", 1) <<"\n";

        } else {
            cout << "Received data: " << recvbuf << "\n";
            if (serWrite(uart_handle, recvbuf, 1) != 0) {
                cout << "sending failed" << endl;
            }
        }

        usleep(10000); // 10msのスリープ

    }



    //終了手続き
    close(sock);
    serClose(uart_handle);
    gpioTerminate();

    return 0;
}


void thread_cv(int port, int WIDTH, int HEIGHT, int num, int ratio)
{
    // ソケットの設定
    int sock;
    struct sockaddr_in addr;
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);             // ポート番号
    addr.sin_addr.s_addr = inet_addr(pc_ip); // 送信先IPアドレス


    // カメラの設定
    VideoCapture cap(num);
    cap.set(CAP_PROP_FRAME_WIDTH, WIDTH);
    cap.set(CAP_PROP_FRAME_HEIGHT, HEIGHT);
    cap.set(CAP_PROP_FPS, 30);

    if (!cap.isOpened())
    {
        cout << "Camera not Found\n!" << endl;
    }

    Mat frame;
    Mat jpgimg;
    static const int sendSize = 65500;      //通信最大パケット数
    char buff[sendSize];
    vector<unsigned char> ibuff;
    vector<int> param = vector<int>(2);
    param[0] = IMWRITE_JPEG_QUALITY;        //jpg使用
    param[1] = ratio;                       //圧縮率


    //メイン部分
    while (waitKey(1) == -1)
    {
        cap >> frame;

        imencode(".jpg", frame, ibuff, param);

        //最大パケット数を越えるとUDPできない
        //複数カメラを使うときは ibuff.size() の合計が65500を越えないように圧縮率で調整する。
        if (ibuff.size() < sendSize)
        {
            for (std::vector<unsigned char>::size_type i = 0; i < ibuff.size(); i++)
                buff[i] = ibuff[i];
            sendto(sock, buff, sendSize, 0, (struct sockaddr*)&addr, sizeof(addr));
            jpgimg = imdecode(Mat(ibuff), IMREAD_COLOR);

        }

        cout << ibuff.size() <<"_"<< num<< "\n";

        //fpsに合わせてる？
        sleep(0.04);

    }
    close(sock);
}