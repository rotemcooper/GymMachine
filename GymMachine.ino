// ---------------------------------------------------------------------------------
// Wiring
// Teensy UART Tx to be connected to light blue next to black wire on Hoverboard
// ---------------------------------------------------------------------------------

typedef enum {
  MOTOR_RIGHT,
  MOTOR_LEFT
} motor_t;

typedef enum {
  HALL_RIGHT,
  HALL_LEFT
} hall_t;

// ---------------------------------------------------------------------------------
// --------------------------------- Golbal Data -----------------------------------
// ---------------------------------------------------------------------------------

// Pin 13 has an LED connected on most Arduino boards.
int led = 13;



// ---------------------------------------------------------------------------------
// ---------------------------- Serial Communication -------------------------------
// ---------------------------------------------------------------------------------

const uint8_t    GYRO_FRAME_LENGTH = 6;
const uint16_t   GYRO_FRAME_START = 256;
const uint8_t    GYRO_CONTACT_CLOSED_BYTE = 85;

void serial_write_frame(HardwareSerial serial, int16_t value)
{
  // Set up a frame simulating gyroscope move on gyro daughterboard
  uint16_t uart_frame[GYRO_FRAME_LENGTH] = {
    GYRO_FRAME_START, 
    (uint8_t)(value & 0xff), 
    (uint8_t)(value >> 8 & 0xff), 
    (uint8_t)(value & 0xff), 
    (uint8_t)(value >> 8 & 0xff), 
    GYRO_CONTACT_CLOSED_BYTE };

  // Send a frame over the UART
  for(uint8_t i=0; i<6; i++) {
    serial.write9bit(uart_frame[i]); 
  } 
}  

//-------------------------------------------------------------------------

// Connect Serial1 Tx to Serial3 Rx
// Connect Serial1 Rx to Serial3 Tx

void serial1_to_serial3_test( uint16_t value )
{
  // Write a frame out of UART1
  serial_write_frame( Serial1, value );

  delay (10);

  // Read fram from UART2 and print via USB
  int incomingByte;
  while (Serial3.available() > 0) {
    incomingByte = Serial3.read();
    Serial.print("UART received: ");
    Serial.println(incomingByte, DEC);
  }
}

// ---------------------------------------------------------------------------------
// ------------------------------------ Motor --------------------------------------
// ---------------------------------------------------------------------------------

inline void motor_right_torque( int16_t value ) {
  serial_write_frame( Serial1, value );
}

inline void motor_left_torque( int16_t value ) {
  serial_write_frame( Serial2, value );
}

inline void motor_both_torque( int16_t value ) {
  serial_write_frame( Serial1, value );
  serial_write_frame( Serial2, value );
}

void motor_wind_back()
{
  bool is_motor_turning = true;
  const uint torque = -100;

  motor_both_torque( torque );
  delay( 2000 );
  
  while( is_motor_turning ) {
    motor_both_torque( torque );
  }
}

//-------------------------------------------------------------------------

void print_hall_sensors()
{  
  int h1 = digitalRead(14);
  int h2 = digitalRead(15);
  int h3 = digitalRead(16);
  Serial.printf("Hall values: %d, %d, %d\n", h1, h2, h3);
}

//-------------------------------------------------------------------------

void motor_up_down_test( int max )
{
  // Loop n times and in between write same value to UART1 multiple times
  
  int i=0;
  const uint loop_cnt = 250;
  const uint loop_delay = 0;
  for(; i<max; i+=10)
  {
    Serial.printf("UART1 Sending: %d\n", i);
    Serial.println( "----------------------------" );
    for(int j=0; j<loop_cnt; j++) {
      delay(loop_delay);
      motor_right_torque( i );
      print_hall_sensors();      
    }    
  }

  for(; i> -max; i-=10)
  {
    Serial.printf("UART1 Sending: %d\n", i);
    Serial.println( "----------------------------" );
    for(int j=0; j<loop_cnt; j++) {
      delay(loop_delay);
      motor_right_torque( i );
      print_hall_sensors(); 
    }    
  }

  for(; i<0; i+=10)
  {
    Serial.printf("UART1 Sending: %d\n", i);
    Serial.println( "----------------------------" );
    for(int j=0; j<loop_cnt; j++) {
      delay(loop_delay);
      motor_right_torque( i );
      print_hall_sensors();      
    }    
  }
}

// ---------------------------------------------------------------------------------
// -------------------------------- Hall Sensors -----------------------------------
// ---------------------------------------------------------------------------------

// Geometry
#define TICKS_PER_ROTATION 89
#define WHEEL_DIAMETER 12.5

// LUT ( hall_tbl[prev_state][state] ) for mapping hall sensors transitions to ticks.
// +1 -> CW tick
// -1 -> CCW tick
// 00 -> no transition
// NA -> invalid transition
// There are 88-90 ticks per revolution
#define NA 99
const signed char hall_tbl[8][8] = { NA, NA, NA, NA, NA, NA, NA, NA,
                                     NA, 00, NA, -1, NA, +1, NA, NA,
                                     NA, NA, 00, +1, NA, NA, -1, NA,
                                     NA, +1, -1, 00, NA, NA, NA, NA,
                                     NA, NA, NA, NA, 00, -1, +1, NA,
                                     NA, -1, NA, NA, +1, 00, NA, NA,
                                     NA, NA, +1, NA, -1, NA, 00, NA,
                                     NA, NA, NA, NA, NA, NA, NA, NA };

 // Enum representing hall sensors states
  // MSB   = hall sensor 1
  // Midel = hall sensor 2
  // LSB   = hall sensor 3
  // At any given time one or two (out of the three) sensors can have a value of 1.
  // The three hall sensor wires should be connected such that the state tansirions
  // match the enum order (from HALL_100 down to HALL101) when the wheel is turning CW.
  enum {
    HALL_100 = 0b100,
    HALL_110 = 0b110,
    HALL_010 = 0b010,
    HALL_011 = 0b011,
    HALL_001 = 0b001,
    HALL_101 = 0b101
  };                                    
                               
// ---------------------------------------------------------------------------------

// Return right motor ticks since start-up.
// This function has to be call frquent enugh to capture all hall sensor transitions.
//
int hall_right_ticks()
{
  static int ticks = 0;
  static uint hall_state = 0;
  static uint hall_state_prev = 0;
  static uint bad_state_cntr = 0;

  int h1 = digitalRead( 14 );
  int h2 = digitalRead( 15 );
  int h3 = digitalRead( 16 );
  hall_state_prev = hall_state;
  hall_state = h1 << 2 | h2 << 1 | h3;

  // Map hall sensors transition to rotation ticks (0, +1, -1)
  // Detect invalid transitions.
  int tick = hall_tbl[hall_state_prev][hall_state];
  if( tick == NA ) {
    bad_state_cntr++;
    tick = 0;
  }
  ticks += tick;
  return ticks;  
}

// ---------------------------------------------------------------------------------

// Return left motor ticks since start-up.
// This function has to be call frquent enugh to capture all hall sensor transitions.
//
int hall_left_ticks()
{
  static int ticks = 0;
  static uint hall_state = 0;
  static uint hall_state_prev = 0;
  static uint bad_state_cntr = 0;

  int h1 = digitalRead( 17 );
  int h2 = digitalRead( 18 );
  int h3 = digitalRead( 19 );
  hall_state_prev = hall_state;
  hall_state = h1 << 2 | h2 << 1 | h3;

  // Map hall sensors transition to rotation ticks (0, +1, -1)
  // Detect invalid transitions.
  int tick = hall_tbl[hall_state_prev][hall_state];
  if( tick == NA ) {
    bad_state_cntr++;
    tick = 0;
  }
  ticks += tick;
  return ticks;  
}

// ---------------------------------------------------------------------------------
// Workout profile table.
// Specifies 8 bits resistance value for each cable pull distance in cm.
byte prf_tbl[] =    {   0,   1,   2,   3,   4,   5,   6,   7,   8,   9,
                       10,  11,  12,  13,  14,  15,  16,  17,  18,  19,
                       20,  21,  22,  23,  24,  25,  26,  27,  28,  29,
                       30,  31,  32,  33,  34,  35,  36,  37,  38,  38,
                       40,  41,  42,  43,  44,  45,  46,  47,  48,  49,
                       50,  51,  52,  53,  54,  55,  56,  57,  58,  59,
                       60,  61,  62,  63,  64,  65,  66,  67,  68,  69,
                       70,  71,  72,  73,  74,  75,  76,  77,  78,  79,
                       80,  81,  82,  83,  84,  85,  86,  87,  88,  89,
                       90,  91,  92,  93,  94,  95,  96,  97,  98,  99,
                      100, 101, 102, 103, 104, 105, 106, 107, 108, 109,
                      110, 111, 112, 113, 114, 115, 116, 117, 118, 119,
                      120, 121, 122, 123, 124, 125, 126, 127, 128, 129,
                      130, 131, 132, 133, 134, 135, 136, 137, 138, 139,
                      140, 141, 142, 143, 144, 145, 146, 147, 148, 149 };

void hall_sensors_test()
{
  int ticks = 0;
  /*
  static int h1;
  static int h2;
  static int h3;
  
  uint hall_state = 0;
  uint hall_state_prev = 0;
  uint bad_state_cntr = 0;
  */
    
  // Wait for motor motion.
  while( hall_right_ticks() == ticks );

  /* 
  while( hall_state == hall_state_prev ||
         hall_tbl[hall_state_prev][hall_state] == NA )
  {
    h1 = digitalRead(14);
    h2 = digitalRead(15);
    h3 = digitalRead(16);
    hall_state_prev = hall_state;
    hall_state = h1 << 2 | h2 << 1 | h3;
    Serial.printf("hall_state NA\n");
  }
  */
  
  // Based on distance cabled is pulled aply torque.
  while( 1 )
  {
    //int i = 0;
    ticks = hall_right_ticks();
    /*
    // Get hall sensors state
    h1 = digitalRead(14);
    h2 = digitalRead(15);
    h3 = digitalRead(16);
    hall_state = h1 << 2 | h2 << 1 | h3;

    // Map hall sensors transition to rotation ticks (0, +1, -1)
    // Detect invalid transitions.
    int tick = hall_tbl[hall_state_prev][hall_state];
    if( tick == NA ) {
      bad_state_cntr++;
      tick = 0;
    }
    ticks += tick;
    */
    
    uint distance = (PI * WHEEL_DIAMETER * ticks) / TICKS_PER_ROTATION;
    if( distance >= sizeof(prf_tbl) ) {
      distance = sizeof(prf_tbl) - 1;
    }
    uint torque = prf_tbl[ distance ];
        
    // Print out hall sensor ticks
    Serial.printf("ticks=%d, distance=%u, torque=%d \n", ticks, distance, torque );
    motor_right_torque( -torque );
    
    //Serial.printf("hall state: %d%d%d\n", h1,h2,h3);
    
    /*
    hall_state_prev = hall_state;
    */
  }
}

//-------------------------------------------------------------------------

void setup() {
  // Initialize serial/USB communication at 9600 bits per second:
  Serial.begin(9600);
  
  // Initialize the UART1 and UART3 - 9 bits mode
  Serial1.begin (26300, SERIAL_9N1);
  Serial3.begin (26300, SERIAL_9N1);

  // Initialize hall sensors
  pinMode(14, INPUT_PULLUP);
  pinMode(15, INPUT_PULLUP);
  pinMode(16, INPUT_PULLUP);

  pinMode(17, INPUT_PULLUP);
  pinMode(18, INPUT_PULLUP);
  pinMode(19, INPUT_PULLUP);

  // initialize the digital pin as an output.
  pinMode(led, OUTPUT);
}

//---------------------------------------------------------------------------

void loop()
{
  // Blink the LED
  digitalWrite(led, HIGH);   // turn the LED on (HIGH is the voltage level)
  //delay(500);               // wait for a second
  //digitalWrite(led, LOW);    // turn the LED off by making the voltage LOW
  //delay(2000);               // wait for a second
  //motor_up_down_test( 1 );
  motor_right_torque( 0 );
  //print_hall_sensors();
  hall_sensors_test();
  Serial.println( "----------------------------" );

  //int incomingByte;
    //while (Serial1.available() > 0) {
    //incomingByte = Serial1.read();
    //Serial.print("UART received: ");
    //Serial.println(incomingByte, DEC);
}
