




## E22

@20220611，王宁所用UM980割草车，基站端/移动站端的电台配置如下：

![E22 config for wangning](images/FreewaveRadio/Freewave_E22_config_wangning.png "E22 config for wangning")


### 配置模式

- 需使用9600波特率，不可使用115200波特率

配置模式下，如果想通过代码，或者串口调试助手来修改其配置，按照其文档，“设置时，只支持9600，8N1格式”

- 用代码配置时，需确保用正常的长度

使用错误长度时，

![wrong length](images/FreewaveRadio/E22_Config_wrong_length.png "wrong length")

![wrong length result](images/FreewaveRadio/E22_Config_wrong_length_result.png "wrong length result")

使用正确长度时，

![right length](images/FreewaveRadio/E22_Config_right_length.png "right length")

![right length result](images/FreewaveRadio/E22_Config_right_length_result.png "right length result")