

1 开机时时间同步

![Time Sync1](images/Luba_systime_gnsstime/time_sync_when_power_on_1.png "Time Sync1")

上图中的 wk0指的是 week0。

故事是这样的，

开机  ->  对rtk进行初始化  ->  rtk初始化完成，直至获取到rtk时间  ->  系统时间 0320-15:14:31.000 看到 RTK时间是 2023-3-20 wk0 7:14:52  ->  做1次时间同步  ->  下一行log 系统时间更新为 “0320-15:14:52.018”。