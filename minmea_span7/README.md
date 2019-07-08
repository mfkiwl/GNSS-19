# GNSS

## NMEA

### NMEA语句

An NMEA sentence consists of a start delimiter, followed by a comma-separated sequence of fields, followed by the character *(ASCII 42), the checksum and an end-of-line marker.

#### NMEA语句最大长度
最大的NMEA语句的长度（包含$，<CR><LF>）是82字节。

## minmea_span7

如果要准确地了解NMEA，那么下面这篇文章是一个很好的阅读材料

https://gpsd.gitlab.io/gpsd/NMEA.html

需求：需要做成gnss_gpsd统一的接口

1. 先看下gnss_gpsd的接口

2. 小武的建议是说做一层 gnss_gpsd.c gnss_gpsd.h 包一层 minmea.h，minmea.c

3. 提供的统一接口最后要输出的是 struct gnss_data_t

4. 之前的gnss_gpsd统一的接口，它依赖于gpsd-3.17，而现在使用基于minmea提供的接口，就不再依赖于gpsd-3.17了。
    需要按照 gnss_reader.h 里面的数据接口 和 interface

    保持数据接口声明不变
    保持get函数的prototype不变

5. 现在数据已经通了，需要我来封装接口




问题：
  a. 如何提供pc版，如何提供TX1版？



现在的进度是：
    大体的架子搭好了，能够编译通过，但运行肯定会报错


现在重点是看运行报错的问题

根据代码Debug