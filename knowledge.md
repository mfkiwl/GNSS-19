








## RBV校准

从IMU坐标系到车体坐标系的旋转量  Rotation from the IMU body frame to the vehicle frame.

在初始化RBV校准之前，需要给RBV校准输入一个有效的RBV估计。至于如何输入一个有效的RBV估计，需要去看SETINSROTATION指令。


# SPAN命令集


## SETINSROTATION

### 作用

指定IMU帧与其他参考帧之间的旋转偏移量

Specifies rotational offsets between the IMU frame and other reference frames

## 语法

SETINSROTATION INSRotation XRotation YRotation ZRotation [XRotationSD] [YRotationSD] [ZRotationSD]

字段INSRotation可能的取值

- RBV  从IMU坐标系到车体坐标系的旋转量

实际命令举例： SETINSROTATION RBV 0 0 90 3.0 3.0 3.0

其实要解决这个问题很简单，首先必须知道IMU坐标系和车体坐标系分别是怎样定义的。






20190704, Foxconn

基站在中兴，松灵两座移动站

基站在富士康，pod_001移动站


1. 当连接不同的基站时，同一个移动站在同一个地理位置得到的GPS location有差别，连接中兴基站与连接富士康基站之间的位置差别大概是在2m左右。
2. 树莓派***/etc/rc.local***这个文件可以配置不同的基站，
    配置成连接富士康基站的语句如下：
        /*
          挂载点的地址：52.82.35.167
          挂载点的端口号：2101
          用户名：FOXCONN
          密码：FOXCONN12345
        */
        nohup /home/pi/RTKLIB/app/str2str/gcc/str2str -in ntrip://FOXCONN:FOXCONN12345@52.82.35.167:2101/PI_FOXCONN -out serial://ttyUSB0:115200:8:n:1 >/dev/null &

        /* 这句话是创建一个热点，热点的名字是pi，密码是perceptin16 */
        sudo create_ap wlan0 eth0 pi perceptin16 &
    配置成连接中兴基站的语句如下：
        /*
          挂载点的地址：52.83.141.234
          挂载点的端口号：2101
          用户名：PI2
          密码：perceptin16
        */
        nohup /home/pi/RTKLIB/app/str2str/gcc/str2str -in ntrip://PI2:perceptin16@52.83.141.234:2101/PI_SZ -out serial://ttyUSB0:115200:8:n:1 >/dev/null &

        /* 这句话是创建一个热点，热点的名字是pi_SCOUT，密码是perceptin16 */
        sudo create_ap wlan0 eth0 pi_SCOUT perceptin16 &



当天线是一左一右时（请参考PTN）

    1. 左边的天线接ATN1，右边的天线接ATN2
    2. 左边的天线给pose
    3. 左边、右边的天线一起给heading




当天线是一前一后时（请参考Vending2）

    1. 前面的天线接到了ATN2，后面的天线接到了ATN1
    2. 后面的天线给pose
    3. 前面、后面的天线一起给heading

    接收机的com2接到了树莓派的USB，树莓派通过网线连到路由器

## 关于树莓派

树莓派的作用是给差分信号

ssh pi@192.168.12.1
password: perceptin

20191126

E, 5: 到5了说明有差分信号了。

## 关于4G上网卡终端

访问网页：
http://192.168.8.1/html/home.html

用户名：admin
密码：perceptin16

## Trouble-shooting
a. 
<img src="images/gps_error_usb_permission.jpg" alt="drawing" width="459" height="612"/>

出现这个问题时，/dev/ttyUSB0的权限是660
/dev/ttyUSB0没有权限
解决方法：
  sudo chmod 777 /dev/ttyUSB0
