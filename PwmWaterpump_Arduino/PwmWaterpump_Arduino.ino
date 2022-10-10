int PWM_PIN = 9;
int pwmval = 0;
#include <Servo.h> // 建立一個 Servo 物件
Servo myservo; // 旋轉角度
int value = -180;

void setup() {
  Serial.begin(9600);
  pinMode(PWM_PIN, OUTPUT);
  Serial.println("Send a value between 1 and 255");
  myservo.attach(5); // Servo 接在 pin 9
  digitalWrite(5, HIGH);
}

void loop() {

  analogWrite(5, 10);
  myservo.write(value); //馬達到90度位置
  //255=5V,255/5=51=1V
  if (Serial.available() > 1) {
    pwmval =  Serial.parseInt();
    Serial.print("Set Speed to: ");
    Serial.println(pwmval);
    analogWrite(PWM_PIN, pwmval);
    //analogWriteFreq(20);
    Serial.println("done!");
  }
}
