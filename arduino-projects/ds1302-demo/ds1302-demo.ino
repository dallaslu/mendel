#include <DS1302RTC.h>
DS1302RTC rtc(D11, D12, D13);//创建DS1302的对象，即对应的Pin
tmElements_t tm; //设置时间类tm
void setup() {
  Serial.begin(9600);
  rtcTC.writeEN(false);//必须设置成false，否则无法设置时间
  rtc.haltRTC(false);//防止时间停止
  setTime(20, 30, 30, 20, 6, 17); //设置系统时间函数，顺序为：时分秒日月年，默认从2000年开始
  rtc.set(now());//将系统时间赋值给DS1302
}
void loop() {
  rtc.read(tm);//读取当前时间
  printTime();//通过串口打印时间
  delay(1000);
}

void printTime() {  //创建打印时间的函数
  Serial.print(tm.Hour);
  Serial.print(':');
  Serial.print(tm.Minute);
  Serial.print(':');
  Serial.print(tm.Second);
  Serial.print(',');
  Serial.print(tm.Day);
  Serial.print('-');
  Serial.print(tm.Month);
  Serial.print('-');
  Serial.println(tmYearToCalendar(tm.Year));//年份比较特殊，需要转化为可读形式
}
