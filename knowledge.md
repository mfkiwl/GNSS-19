
20190704, Foxconn

基站在中兴，松灵两座移动站

基站在富士康，pod_001移动站


1. 当连接不同的基站时，同一个移动站在同一个地理位置得到的GPS location只有有差别，连接中兴基站与连接富士康基站之间的位置差别大概是在2m左右。
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