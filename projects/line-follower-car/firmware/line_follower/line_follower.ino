/*
 * 循迹小车 · 示例程序（Arduino UNO R3 + L298N + 2 路循迹）
 * ------------------------------------------------------------------
 * 逻辑（一句话）：两个探头都看不到黑线就直走；哪一边看到黑线就往哪边拐。
 *
 * 目标读者：第一次做机器人的孩子（家长讲解用）。每一行都有中文说明。
 * 想改行为，只动下面【可调参数】区，别动 setup()/loop() 的结构。
 *
 * 接线（与 docs/03-组装与调试.md 一致）：
 *   L298N ENA -> D5(PWM)   IN1 -> D7   IN2 -> D8   左电机 OUT1/OUT2
 *   L298N ENB -> D6(PWM)   IN3 -> D9   IN4 -> D10  右电机 OUT3/OUT4
 *   循迹左探头 -> A0       循迹右探头 -> A1
 *   电池+ -> L298N 12V    电池- -> L298N GND -> UNO GND（必须共地）
 * ------------------------------------------------------------------
 */

// ======================= 【可调参数】 =======================

// 判定"探头看到黑线"的阈值。黑线读数与白底读数的中间值。
// 例：白底约 900、黑线约 120，则取 (900+120)/2 = 510。
const int THRESHOLD = 500;

// 你的探头是"黑线读数偏小"还是"偏大"？默认黑线读数偏小（多数模块如此）。
// 若实测相反（黑线上读数反而大），把这里改成 false。
const bool BLACK_IS_LOW = true;

// 速度（0=不动, 255=最快）。跑太快容易掉线，先从慢的开始试。
const int SPEED_NORMAL = 150;  // 直行速度
const int SPEED_TURN   = 110;  // 转弯时"慢的那一侧轮子"的速度
const int SPEED_FAST   = 190;  // 转弯时"快的那一侧轮子"的速度

// 串口打印间隔（毫秒）。调小=刷得更快，调大=看着更舒服。
const unsigned long PRINT_INTERVAL = 200;

// ======================= 【引脚定义】 =======================
const int ENA = 5,  IN1 = 7, IN2 = 8;    // 左轮：调速 + 两个方向脚
const int ENB = 6,  IN3 = 9, IN4 = 10;   // 右轮：调速 + 两个方向脚
const int PIN_L = A0;                     // 左循迹探头（模拟读数）
const int PIN_R = A1;                     // 右循迹探头（模拟读数）

// ======================= 【小工具函数】 =======================

// 读一次探头，返回 true = 看到黑线
bool seesBlack(int pin) {
  int v = analogRead(pin);                // 0..1023 的原始读数
  if (BLACK_IS_LOW) return v <  THRESHOLD;
  else              return v >= THRESHOLD;
}

// 让左轮按 speed 速度转；direction=1 正转，-1 反转
void setLeft(int speed, int direction) {
  analogWrite(ENA, speed);                // 调速：占空比决定快慢
  digitalWrite(IN1, direction > 0 ? HIGH : LOW);
  digitalWrite(IN2, direction > 0 ? LOW  : HIGH);
}

// 让右轮按 speed 速度转
void setRight(int speed, int direction) {
  analogWrite(ENB, speed);
  digitalWrite(IN3, direction > 0 ? HIGH : LOW);
  digitalWrite(IN4, direction > 0 ? LOW  : HIGH);
}

// 三个基本动作，起个名字让孩子好记
void goStraight() { setLeft(SPEED_NORMAL, 1); setRight(SPEED_NORMAL, 1); }
void turnLeft()   { setLeft(SPEED_TURN,   1); setRight(SPEED_FAST,   1); } // 左轮慢、右轮快 -> 往左拐
void turnRight()  { setLeft(SPEED_FAST,   1); setRight(SPEED_TURN,   1); } // 右轮慢、左轮快 -> 往右拐
void stopCar()    { setLeft(0, 1); setRight(0, 1); }

// ======================= 【主程序】 =======================

void setup() {
  // 把用到的脚设成"输出"（我们要用它去控制电机）
  pinMode(ENA, OUTPUT); pinMode(IN1, OUTPUT); pinMode(IN2, OUTPUT);
  pinMode(ENB, OUTPUT); pinMode(IN3, OUTPUT); pinMode(IN4, OUTPUT);
  pinMode(PIN_L, INPUT); pinMode(PIN_R, INPUT);

  Serial.begin(9600);                     // 打开串口，方便在电脑上看数值
  Serial.println("循迹小车启动：把探头对着黑线/白底看数值");
  delay(1000);                            // 上电先停 1 秒，给人反应时间
}

void loop() {
  bool L = seesBlack(PIN_L);              // 左边看不看到黑线
  bool R = seesBlack(PIN_R);              // 右边看不看到黑线
  const char* state;                      // 用来打印当前状态

  // ---- 决策规则（只有四条，孩子能背下来）----
  if (!L && !R) {                         // 两边都没看到 -> 线在中间，直走
    goStraight();
    state = "直行";
  } else if (L && !R) {                   // 只有左边看到 -> 往左拐
    turnLeft();
    state = "左拐";
  } else if (!L && R) {                   // 只有右边看到 -> 往右拐
    turnRight();
    state = "右拐";
  } else {                                // 两边都看到 -> 十字/宽线，直走冲过去
    goStraight();
    state = "直行(双黑)";
  }

  // ---- 每 200ms 往串口打印一次，方便调阈值 ----
  static unsigned long last = 0;
  if (millis() - last >= PRINT_INTERVAL) {
    last = millis();
    Serial.print("L="); Serial.print(analogRead(PIN_L));
    Serial.print(" R="); Serial.print(analogRead(PIN_R));
    Serial.print(" | 状态: "); Serial.println(state);
  }
}
