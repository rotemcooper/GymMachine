
// ---------------------------------------------------------------------------------
// Configurations
// ---------------------------------------------------------------------------------

// Only one fo these to be defined
// Relay Haverboard Serial1 UART communication viae Serial/USB
//#define UART_PASSTHROUGH

// New FW in Hoverboard (as opposed to original FW)
#define NEW_FW

// Act as Serial1<->USB adapter
#define UART_ADAPTER

// ---------------------------------------------------------------------------------
// Wiring
// Teensy UART Tx to be connected to light blue next to black wire on Hoverboard
// ---------------------------------------------------------------------------------

typedef enum {
  MOTOR_RIGHT=0,
  MOTOR_LEFT=1
} motor_t;

typedef enum {
  HALL_RIGHT,
  HALL_LEFT
} hall_t;

typedef enum {
  DIRECTION_PULL,
  DIRECTION_REL
} direction_t;

typedef enum {
  CMD_PRF_CHANGE,
  CMD_STOP
} cmd_t;

class WorkoutPrf {
  private:
  const int addPullInit;
  const int addRelInit;
  const int multPullInit;
  const int multRelInit;   
  
  public:
  const char* const name;
  const uint len;
  const byte* const tbl;
  int addPull;
  int addRel;
  int multPull;
  int multRel;
  WorkoutPrf( const char* const namePrm,
                 int addPullInitPrm,
                 int addRelInitPrm,
                 int multPullInitPrm,
                 int multRelInitPrm,
                 uint lenPrm,
                 const byte* const tblPrm ) :
    name( namePrm ),
    addPullInit( addPullInitPrm ),
    addRelInit( addRelInitPrm ),
    multPullInit( multPullInitPrm ),
    multRelInit( multRelInitPrm ),
    len( lenPrm ),
    tbl( tblPrm )
  {
    reset();
  }

    void reset() {
      addPull = addPullInit;
      addRel = addRelInit;
      multPull = multPullInit;
      multRel = multRelInit;
    }
};

/*
void workout_pref_init( WorkoutPrf *prf ) {
  prf->add_pull = prf->add_pull_init;
  prf->add_rel = prf->add_rel_init;
  prf->mult_pull = prf->mult_pull_init;
  prf->mult_rel = prf->mult_rel_init;
}
*/

// ---------------------------------------------------------------------------------
// ---------------------------- Serial Communication -------------------------------
// ---------------------------------------------------------------------------------

#ifdef NEW_FW
void serialWriteFrame( HardwareSerial* serial, int16_t value )
{
  char motor = 'r';
  if( serial == &Serial1 ) {
    motor = 'l';
  }

  Serial1.write( 'm' );
  Serial1.write( motor );
  Serial1.write( (uint8_t)(value & 0xff) );
  Serial1.write( (uint8_t)((value >> 8) & 0xff) );
  Serial1.write( 'e' );
}

#else

const uint8_t    SERIAL_FRAME_LENGTH = 6;
const uint16_t   SERIAL_FRAME_START = 256;
const uint8_t    SERIAL_CONTACT_CLOSED_BYTE = 85;

void serialWriteFrame( HardwareSerial* serial, int16_t value )
{
  // Set up a frame
  uint16_t uartFrame[ SERIAL_FRAME_LENGTH ] = {
    SERIAL_FRAME_START, 
    (uint8_t)(value & 0xff), 
    (uint8_t)(value >> 8 & 0xff), 
    (uint8_t)(value & 0xff), 
    (uint8_t)(value >> 8 & 0xff), 
    SERIAL_CONTACT_CLOSED_BYTE };

  // Send the frame over the UART
  for( uint8_t i=0; i<6; i++ ) {
    serial->write9bit( uartFrame[i] ); 
  } 
}
#endif

//-------------------------------------------------------------------------

// Serial1 and Serial3 testing.
// Connect Serial1 Tx to Serial3 Rx
// Connect Serial1 Rx to Serial3 Tx

void serial1ToSerial3Test( uint16_t value )
{
  // Write a frame out of UART1
  serialWriteFrame( &Serial1, value );

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
// -------------------------------- Hall Sensors -----------------------------------
// ---------------------------------------------------------------------------------

// Right motor hall wires to Teensy 3.2 pins
// Blue wire -> pin14
// Green wire -> pin15
// Yellow wire -> pin16
//
// Left motor hall wires to Teensy 3.2 pins
// Blue wire -> pin18
// Green wire -> pin19
// Yellow wire -> pin20//

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

// Interval (in millisec) to compute speed and acceleration
#define HALL_INTERVAL 50

class Hall {
  private:
  int h1Pin;
  int h2Pin;
  int h3Pin;
  uint state;
  uint statePrev;
  uint badStateCntr;
  int ticksCntr;
  int speedCntr;
  int accelCntr;
  struct {
    unsigned long time;
    int ticks;
    int speed;
  } prev;  
      
  public:
  Hall( int h1PinPrm, int h2PinPrm, int h3PinPrm ) :
    h1Pin( h1PinPrm ),
    h2Pin( h2PinPrm ),
    h3Pin( h3PinPrm ) {
      reset();
      state = 0;
      statePrev = 0;
      prev.time = millis();
      prev.ticks = 0;
      prev.speed = 0;
  }

  //--------------------------------------------------------------------------------
  
  void reset() {
    badStateCntr = 0;
    ticksCntr = 0;
    speedCntr = 0;
    accelCntr = 0;  
  }

 //--------------------------------------------------------------------------------
 
  // Return hall sensor ticks since reset.
  // This function has to be call frquent enugh to capture all hall sensor transitions.
  // Blue wire -> h1Pin
  // Green wire -> h2Pin
  // Yellow wire -> h3Pin
  //
  int ticks() {
    int h1 = digitalRead( h1Pin );
    int h2 = digitalRead( h2Pin );
    int h3 = digitalRead( h3Pin );
    statePrev = state;
    state = h1 << 2 | h2 << 1 | h3;

    // Map hall sensors transition to rotation ticks (0, +1, -1)
    // Detect invalid transitions.
    int tick = hall_tbl[statePrev][state];
    if( tick == NA ) {
      badStateCntr++;
      tick = 0;
    }
    ticksCntr += tick;

    //--------------------------------------------------------------------------------

    // Calculate ticks speed and acceleration
    unsigned long timeCurr = millis();
    if( timeCurr >= prev.time + HALL_INTERVAL ) {
      speedCntr = (speedCntr + (int)((ticksCntr - prev.ticks) * (1000/HALL_INTERVAL)))/2;
      accelCntr = (3*accelCntr + (int)((speedCntr - prev.speed) * (1000/HALL_INTERVAL)))/4;
      prev.ticks = ticksCntr;
      prev.speed = speedCntr;
      prev.time = timeCurr;
    }

    return ticksCntr; 
  }
  
  inline uint badState() {
    return badStateCntr;
  }
  
  inline int speed() {
    return speedCntr;
  }

  inline int accel() {
    return accelCntr;
  }

  void print() {  
    int h1 = digitalRead(h1Pin);
    int h2 = digitalRead(h2Pin);
    int h3 = digitalRead(h3Pin);
    Serial.printf("Hall values: %d, %d, %d\n", h1, h2, h3);
  }
};

// ---------------------------------------------------------------------------------

void hallSensorsTest()
{
  Hall rightHall( 14, 15, 16 );
  Hall leftHall( 18, 19, 20 );
  while( 1 ) {
    Serial.printf( "right ticks/bad_ctr=%d/%d, left ticks/bad_ctr=%d/%d\n",\
                    rightHall.ticks(), rightHall.badState(),\
                    leftHall.ticks(), leftHall.badState() );
  }  
}

// ---------------------------------------------------------------------------------
// ------------------------------------ Motor --------------------------------------
// ---------------------------------------------------------------------------------

class Motor {
  private:
  HardwareSerial* serial;
  int16_t valueLast;
  int16_t valueSent;

  public:
  Hall hall;
  Motor( HardwareSerial* serialPrm,
         int hallH1Pin,
         int hallH2Pin,
         int hallH3Pin ) :
    serial( serialPrm ),
    valueLast( 0 ),
    valueSent( 0 ),
    hall( hallH1Pin, hallH2Pin, hallH3Pin ) {
    }

  // ---------------------------------------------------------------------------------
  
  inline void torque( int16_t value ) {
    serialWriteFrame( serial, -value );
  }

  // ---------------------------------------------------------------------------------

  void torqueSmooth( int16_t value ) {
    int diff = value - valueLast;
    if( diff > 0 ) {
      valueLast += 2;        
    }
    else if( diff < 0 ) {
      valueLast -= 2;     
    }

    if( valueSent != valueLast ) {
      valueSent = valueLast;
      torque( valueLast );   
    }
     
  }

  // ---------------------------------------------------------------------------------

  void service( int16_t value ) {
    hall.ticks();
    torqueSmooth( value );  
  }  
};

// ---------------------------------------------------------------------------------

class Motors {
  private:
  
  public:
  Motor right;
  Motor left;
  Motors() :
    right( &Serial3, 14, 15, 16 ), // Serial and Hall sensors pins for right motor
    left( &Serial1, 18, 19, 20 ) { // Serial and Hall sensors pins for left motor
  }

  // ---------------------------------------------------------------------------------
  
  inline void reset() {
    right.hall.reset();
    left.hall.reset();  
  }
  
  // ---------------------------------------------------------------------------------
  
  inline void torque( int16_t value ) {
    right.torque( value );
    left.torque( value );
  }
  
  inline void torqueSmooth( int16_t value ) {
    right.torqueSmooth( value );
    left.torqueSmooth( value );
  }

  // ---------------------------------------------------------------------------------

  inline void service( int16_t value ) {
    right.service( value );
    left.service( value );
  }

  // ---------------------------------------------------------------------------------
  
  void windBack( int torquePrm )
  {
    int rightTicks;
    int leftTicks;

    // Continue to apply torque as long as motors are turning.
    do {
      rightTicks = right.hall.ticks();
      leftTicks = left.hall.ticks();
      Serial.printf("Windback torque=%d\n", torquePrm);
      serial1Rx(); //rotemc
      for(int i=0; i<100; i++) {
        torqueSmooth( torquePrm );
        right.hall.ticks();
        left.hall.ticks();
      }
    } while( rightTicks > right.hall.ticks() ||
             leftTicks > left.hall.ticks() );
    torque( 0 );
  }
};

//-------------------------------------------------------------------------

void motorsUpDownTest( int max )
{
  // Loop n times and in between write same value to UART1 multiple times
  Motors motors;
  
  int i=0;
  const uint loopCnt = 250;
  const uint loopDelay = 0;
  for(; i<max; i+=10)
  //for(; i<max; i+=1 )
  {
    Serial.printf("UART1 Sending: %d\n", i);
    Serial.println( "----------------------------" );
    for(int j=0; j<loopCnt; j++) {
      delay(loopDelay);
      motors.torque( i );
      //motors.right.hall.print();      
    }    
  }

  for(; i> -max; i-=10)
  {
    Serial.printf("UART1 Sending: %d\n", i);
    Serial.println( "----------------------------" );
    for(int j=0; j<loopCnt; j++) {
      delay(loopDelay);
      motors.torque( i );
      //motors.right.hall.print(); 
    }    
  }

  for(; i<0; i+=10)
  {
    Serial.printf("UART1 Sending: %d\n", i);
    Serial.println( "----------------------------" );
    for(int j=0; j<loopCnt; j++) {
      delay(loopDelay);
      motors.torque( i );
      //motors.right.hall.print();      
    }    
  }
}

// ---------------------------------------------------------------------------------
// ------------------------------ Workout Profiles ---------------------------------
// ---------------------------------------------------------------------------------

// Workout profiles.
// Specifies 8 bits resistance value for each cable pull distance in cm.
 
#define W1 50
byte weight_tbl[] = {/*0,   0,   0,   0,   0,   0,   0,   0,*/  0,   0,
                       W1,  W1,  W1,  W1,  W1,  W1,  W1,  W1,  W1,  W1,
                       W1,  W1,  W1,  W1,  W1,  W1,  W1,  W1,  W1,  W1,
                       W1,  W1,  W1,  W1,  W1,  W1,  W1,  W1,  W1,  W1,
                       W1,  W1,  W1,  W1,  W1,  W1,  W1,  W1,  W1,  W1,
                       W1,  W1,  W1,  W1,  W1,  W1,  W1,  W1,  W1,  W1,
                       W1,  W1,  W1,  W1,  W1,  W1,  W1,  W1,  W1,  W1,
                       W1,  W1,  W1,  W1,  W1,  W1,  W1,  W1,  W1,  W1,
                       W1,  W1,  W1,  W1,  W1,  W1,  W1,  W1,  W1,  W1 };

WorkoutPrf weight_prf( "Weight", 0, 0, 4, 4, sizeof(weight_tbl), weight_tbl );

// ---------------------------------------------------------------------------------

byte spring_tbl[] = {   /*0,   0,   0,   0,   0,   0,   0,   0,   0,   0,*/
                        0,   1,   2,   3,   4,   5,   6,   7,   8,   9,
                       10,  11,  12,  13,  14,  15,  16,  17,  18,  19,
                       20,  21,  22,  23,  24,  25,  26,  27,  28,  29,
                       30,  31,  32,  33,  34,  35,  36,  37,  38,  39,
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

WorkoutPrf spring_prf( "Spring", 0, 0, 4, 4, sizeof(spring_tbl), spring_tbl );

// ---------------------------------------------------------------------------------

byte inv_spring_tbl[] = {/*  0,  0,  0,  0,  0,  0,  0,  0,*/  0,  0,
                          149,148,147,146,145,144,143,142,141,140,
                          139,138,137,136,135,134,133,132,131,130,
                          129,128,127,126,125,124,123,122,121,120,
                          119,118,117,116,115,114,113,112,111,110,
                          109,108,107,106,105,104,103,102,101,100,
                          99,98,97,96,95,94,93,92,91,90,
                          89,88,87,86,85,84,83,82,81,80,
                          79,78,77,76,75,74,73,72,71,70,
                          69,68,67,66,65,64,63,62,61,60,
                          59,58,57,56,55,54,53,52,51,50,
                          49,48,47,46,45,44,43,42,41,40,
                          39,38,37,36,35,34,33,32,31,30,
                          29,28,27,26,25,24,23,22,21,20 };

WorkoutPrf inv_spring_prf( "Inv-Spring", 0, 0, 2, 2, sizeof(inv_spring_tbl), inv_spring_tbl );

// ---------------------------------------------------------------------------------

byte mtn_tbl[] = {  /* 0,   0,   0,   0,   0,   0,   0,   0,*/   0,   0,
                    50,  52,  54,  56,  58,  60,  62,  64,  66,  68,
                    70,  72,  74,  76,  78,  80,  82,  84,  86,  88,
                    90,  92,  94,  96,  98, 100, 102, 104, 106, 108,
                   110, 112, 114, 116, 118, 120, 122, 124, 126, 128,
                   128, 126, 124, 122, 120, 118, 116, 114, 112, 110,
                   108, 106, 104, 102, 100,  98,  96,  94,  92,  90,
                    88,  86,  84,  82,  80,  78,  76,  74,  72,  70,
                    68,  66,  64,  62,  60,  58,  56,  54,  52,  50 };
       
WorkoutPrf mtn_prf( "Mountain", 0, 0, 4, 4, sizeof(mtn_tbl), mtn_tbl );

// ---------------------------------------------------------------------------------

byte v_tbl[] =   {/*0,   0,   0,   0,   0,   0,   0,   0,*/  0,   0,
                   128, 126, 124, 122, 120, 118, 116, 114, 112, 110,
                   108, 106, 104, 102, 100,  98,  96,  94,  92,  90,
                    88,  86,  84,  82,  80,  78,  76,  74,  72,  70,
                    68,  66,  64,  62,  60,  58,  56,  54,  52,  50,
                    50,  52,  54,  56,  58,  60,  62,  64,  66,  68,
                    70,  72,  74,  76,  78,  80,  82,  84,  86,  88,
                    90,  92,  94,  96,  98, 100, 102, 104, 106, 108,
                   110, 112, 114, 116, 118, 120, 122, 124, 126, 128 };
       
WorkoutPrf v_prf( "V-Shape", 0, 0, 4, 4, sizeof(v_tbl), v_tbl );

// ---------------------------------------------------------------------------------
// ----------------------------------- Cable ---------------------------------------
// ---------------------------------------------------------------------------------

// Geometry
#define TICKS_PER_ROTATION 89.0
#define WHEEL_DIAMETER 12.5
#define DIRECTION_COMP 100

class Cable {
  private:
  Motor* motor;
  WorkoutPrf* prf;
  int ticks;
  int distanceRaw;
  int distance;
  int torque;
  direction_t direction;
  int speed;
  int speedPrev;
  int directionComp;

  public:
  Cable( Motor* motorPrm, WorkoutPrf* prfPrm ) :
    motor( motorPrm ),
    prf( prfPrm ),
    ticks( 0 ),
    distanceRaw( 0 ),
    distance( 0 ),
    torque( 0 ),
    direction( DIRECTION_PULL ),
    speed( 0 ),
    speedPrev( 0 ),
    directionComp( 0 ) {
  }

  // ---------------------------------------------------------------------------------

  void setPrf( WorkoutPrf* prfPrm ) {
    prf = prfPrm;
    direction = DIRECTION_PULL;
    speedPrev = 0; 
  }

  // ---------------------------------------------------------------------------------
  
  inline int dist() {
    return distance;
  }

  inline int distRaw() {
    return distanceRaw;
  }

  inline int torq() {
    return torque;
  }

  inline int torqMin() {
    return DIRECTION_COMP;
  }

  inline direction_t dir() {
    return direction;
  }

  // ---------------------------------------------------------------------------------
  
  inline int dirComp( int comp ) {
           
    if( comp > 0 ){
      if( directionComp/2 < comp ) directionComp++;
    }
    else {
      if( directionComp > 0 ) directionComp--;
    }  
    return directionComp/2;
  }
  
  // ---------------------------------------------------------------------------------
  
   int torqCompute() {
    //rotemc direction = DIRECTION_PULL;
    
    //speedPrev = 0;
    
    ticks = motor->hall.ticks();
    distanceRaw = (int) ((PI * WHEEL_DIAMETER * ticks) / TICKS_PER_ROTATION);
    distance = constrain( distanceRaw, 0, (prf->len - 1) );
    torque = prf->tbl[ distance ];
      
    int speed = motor->hall.speed();
    if( speed > 0 ) {
      direction = DIRECTION_PULL;
    }
    else if( speed < 0 ) {
      direction = DIRECTION_REL;
    }
    /*
    else if( speedPrev > 0 ) {
      direction = DIRECTION_REL;
    }
    else if( speedPrev < 0 ) {
      direction = DIRECTION_PULL;
    }
    */
   
    speedPrev = speed;
    
    if( direction == DIRECTION_PULL ) {
      torque *= prf->multPull;
      if( torque != 0 ) {
        torque += prf->addPull;
        torque += dirComp( 0 );
      }
      //torque -= motor->hall.accel()/4;              
    }
    else {  
      torque *= prf->multRel;
      if( torque != 0 ) {
        torque += prf->addRel;
        torque += dirComp( DIRECTION_COMP );        
      }
      //torque -= motor->hall.accel()*4;           
    }
    torque -= speed;
    torque -= motor->hall.accel()/4; 
    
    if( (distance < 3) || 
        (distance > 30 && speed <= 0) )
    {    
      torque = max( torque, DIRECTION_COMP);  
    }
        
    if( distance <= -50 ) {
      torque = 0;
    }
    
    torque = max( torque, -400 ); //rotemc
    return torque;
    //motor->torqueSmooth( torque );
  }

  // ---------------------------------------------------------------------------------

  inline void torqApply( int torq ) {
    torque = torq;
    motor->torqueSmooth( torq );        
  }

  // ---------------------------------------------------------------------------------
  
  
  inline void workout() {
    torqCompute();
    torqApply( torque );
  }  
};

// ---------------------------------------------------------------------------------
// ---------------------------------- Machine --------------------------------------
// ---------------------------------------------------------------------------------

class Machine {
  private:
  Motors motors;
  WorkoutPrf* prf;
  Cable rightCable;
  Cable leftCable;  
    
  // ---------------------------------------------------------------------------------
  
  public:
  Machine() :
    prf( &weight_prf ),
    rightCable( &motors.right, prf ),
    leftCable( &motors.left, prf )
  {}

  // ---------------------------------------------------------------------------------
  
  void workout( WorkoutPrf* prfPrm ) {
    
    prf = prfPrm;
    rightCable.setPrf( prf );
    leftCable.setPrf( prf );

    int cnt= 0;
    int printCnt = 0;
    while( continueWorkout() )
    {
      //---------------- new ---------------------------------
      
      rightCable.workout();
      leftCable.workout();
      /*      
      int rightTorq = rightCable.torqCompute();
      int leftTorq = leftCable.torqCompute();
      if( rightCable.distRaw() > 50 && leftCable.distRaw() > 50 )
      {
        if( rightCable.distRaw() > leftCable.distRaw()  ) {
          //leftTorq = leftCable.torqMin();
          if( leftCable.torq() > leftCable.torqMin()+50 ) {
            rightTorq += 50;
            leftTorq -=50;
          }          
        }
        else if( leftCable.distRaw() > rightCable.distRaw() ) {
          //rightTorq = rightCable.torqMin();
          if( rightCable.torq() > rightCable.torqMin()+50 ) {
            rightTorq -= 50;
            leftTorq +=50;
          }
        }
        else {
          //rightTorq /= 2;
          //leftTorq /= 2;
        }
      }
      rightCable.torqApply( rightTorq );
      leftCable.torqApply( leftTorq );
      */
      
      //---------------- new ---------------------------------
            
      if( ++printCnt > 12 ) //12
      {
        printCnt=0;
        cnt++;
        
        // Print ticks, distance, torque, etc.
        Serial.printf("cnt=%d, prf=%s, add=%d/%d, mult=%d/%d, ticks=%d/%d, dist=%d/%d, speed=%d/%d, accel=%d/%d, torque=%d/%d\n",
                     cnt++, prf->name, prf->addPull, prf->addRel, prf->multPull, prf->multRel, motors.right.hall.ticks(), motors.left.hall.ticks(),
                     rightCable.distRaw(), leftCable.distRaw(), motors.right.hall.speed(), motors.left.hall.speed(),
                     motors.right.hall.accel()/10, motors.left.hall.accel()/10, rightCable.torq(), leftCable.torq() );
      }
    }
  }

  // ---------------------------------------------------------------------------------

  inline void serviceMotors() {
    motors.service( 150 );      
  }

  // ---------------------------------------------------------------------------------

  void waitForStart()
  {
    // Wait for cables pull.
    while( continueWorkout() && motors.right.hall.ticks() == 0 && motors.left.hall.ticks() == 0 ) {
      Serial.printf("Waiting for cables pull...\n");
      motors.torque( 0 );  
    }   
  }

   // ---------------------------------------------------------------------------------

  void printCmd( char cmd )
  {
    for( int i=0; i<1000; i++ ) {
      serviceMotors();
      Serial.printf("received %c\n", cmd );  
    }
  }

  // ---------------------------------------------------------------------------------

  void strengthTest()
  {
    static const int convTbl[][2] = { {225, 30}, {300, 35}, {350, 40}, {400, 45}, {450, 50}, {500, 55}, {600, 60}, {750, 65}, {0, 0} };
    motors.windBack( 150 );
    motors.reset();
    Motor* motor = &motors.right;
    Motor* motorOther = &motors.left;

    while( continueWorkout() ) {
      motors.service( 0 );
      Serial.printf("Waiting for cable pull...\n" );
      if( motors.right.hall.ticks() > 50 ) {
        motor = &motors.right;
        motorOther = &motors.left;
        break;        
      }
      else if( motors.left.hall.ticks() > 50 ) {
        motor = &motors.left;
        motorOther = &motors.right;
        break;        
      }
    }    
    
    while( continueWorkout() && motor->hall.speed() > 0 ) {
      motors.service( 50 );
      Serial.printf("Stop pulling for strenght test start...\n" );
    }

    int ticks = motor->hall.ticks();
    int torque = 150;
    int torqueMax = torque;
    while( continueWorkout() && motor->hall.ticks() > 50 && ticks > 50 &&
         (motor->hall.ticks() + 50) > ticks )
    {
      Serial.printf("Strength test torque=%d\n", torque );
      for(int i=0; i<100; i++) {
        motor->hall.ticks();
        motorOther->hall.ticks();
        motor->torqueSmooth( torque );
        motorOther->torqueSmooth( 150 );        
      }      
      torqueMax = max( torqueMax, torque );
      torque += 10;
    }

    int poundMax = convTbl[0][1];
    for(int i=0; convTbl[i][0] > 0; i++) {
      if( convTbl[i][0] > torqueMax ) {
        break;
      }
      poundMax = convTbl[i][1];
    }
  
    while( continueWorkout() ) {
      Serial.printf("Strength test done, max torque=%d, lb=%d\n", torqueMax, poundMax );
      for(int i=0; i<100; i++) {
        motor->hall.ticks();
        motorOther->hall.ticks();
        motors.torque( 150 );
      }      
      //delay( 200 );
    }
  }  

 // ---------------------------------------------------------------------------------
  
  int getNum() {
    int num=0;
    while( Serial.available() )
    {
      char next = Serial.peek();
      if( next >= '0' && next <= '9' ) {
        num *= 10;
        num += (next - '0');
        Serial.read();
      }
      else {
        return num; 
      }
    }
    return num;
  }

  // ---------------------------------------------------------------------------------

  void prfAdjust( int* add, int* mult )
  { 
    char prev = 0;
    while( Serial.available() )
    {
      switch( Serial.peek() )
      {
        case '+':
          prev = Serial.read();
          *add += 10;          
          break;
            
        case '-':
          prev = Serial.read();
          *add -= 10;
          break;

        case '*':
          prev = Serial.read();
          *mult += 1;
          break;
      
        case '/':
          prev = Serial.read();
          *mult = constrain( *mult-1, 1, *mult );
          break;

        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
          if( prev == '+' || prev == '-' ) {
            *add = getNum();
          }
          else if( prev == '*' || prev == '/' ) {
            *mult = getNum();
          }
          else {
            return; // To be handeled at outer loop
          }
          break;          
        
        default:
          return; // To be handeled at outer loop
      }      
    }     
  }

  // ---------------------------------------------------------------------------------

  bool continueWorkout()
  {
    serial1Rx(); //rotemc
    
    if( !Serial.available() ) {
      return true;
    }

    switch( Serial.peek() ) {
      case '\n':
      case '\r':
        Serial.read();
        return true;
      
      case '0':
        Serial.read();
        prf->reset();
        return true;
      
      case '+':
        Serial.read();
        prf->addPull += 10;
        prf->addRel += 10;
        return true;

      case '-':
        Serial.read();
        prf->addPull -= 10;
        prf->addRel -= 10;
        return true;  
      
      case '*':
        Serial.read();
        prf->multPull += 1;
        prf->multRel += 1;
        return true;

      case '/':
        Serial.read();
        prf->multPull = constrain( prf->multPull-1, 1, prf->multPull );
        prf->multRel = constrain( prf->multRel-1, 1, prf->multRel );
        return true;
      
      case 'p':
        Serial.read();
        prfAdjust( &prf->addPull, &prf->multPull );
        return true;
            
      case 'r':
        Serial.read();
        prfAdjust( &prf->addRel, &prf->multRel );
        return true;      
      
      default:
        return false;
    }
    return false;  
  }

  // ---------------------------------------------------------------------------------

  void main()
  {
    waitForStart();
    while(1)
    {
      while( !Serial.available() ) {
        motors.windBack( 150 );
        motors.reset();
        workout( prf );
      }
      
      int input = Serial.read();
      //printCmd( input );
      switch( input )
      {
        case '\n':
        case '\r':
          break;
      
        case 'w':
          prf = &weight_prf;
          break;

        case 's':
          prf = &spring_prf;
          break;

        case 'i':
          prf = &inv_spring_prf;
          break;
        
        case 'm':
          prf = &mtn_prf;
          break;
      
        case 'v':
          prf = &v_prf;
          break;
        
        case 't':
          strengthTest();
          break;
      
        case '0':
          prf->reset();
          break;
          
        case '+':
          prf->addPull += 10;
          prf->addRel += 10;
          break;

        case '-':
          prf->addPull -= 10;
          prf->addRel -= 10;
          break;  
      
        case '*':
          prf->multPull += 1;
          prf->multRel += 1;
          break;

        case '/':
          prf->multPull = constrain( prf->multPull-1, 1, prf->multPull );
          prf->multRel = constrain( prf->multRel-1, 1, prf->multRel );
          break;
      
        case 'p':
          prfAdjust( &prf->addPull, &prf->multPull );
          break;
            
        case 'r':
          prfAdjust( &prf->addRel, &prf->multRel );
          break;

        case 'q':
          Serial1.write( 'q' );
          delay( 200 );
          Serial1.write( 'q' );
          delay( 200 );
          Serial1.write( 'q' );
          break;       
        
        default:
          Serial.printf("received invalid input\n");
          break;
      }    
    }
  }
};

// ---------------------------------------------------------------------------------
// --------------------------------- Main Loop -------------------------------------
// ---------------------------------------------------------------------------------

void setup() {
  // Initialize serial/USB communication
  Serial.begin(9600);
  
  // Initialize the UART1 and UART3
  #if defined(NEW_FW)
    //Serial1.begin ( 9600 );
    Serial1.begin ( 57600 );
  #else
    // 9 bits mode to be used with original hoverboard FW
    Serial1.begin (26300, SERIAL_9N1);
    Serial3.begin (26300, SERIAL_9N1);
  #endif  

  // Initialize hall sensors
  pinMode(14, INPUT_PULLUP);
  pinMode(15, INPUT_PULLUP);
  pinMode(16, INPUT_PULLUP);

  pinMode(18, INPUT_PULLUP);
  pinMode(19, INPUT_PULLUP);
  pinMode(20, INPUT_PULLUP);

  // Hall sensor test pins
  //pinMode(21, INPUT_PULLUP);
  //pinMode(22, INPUT_PULLUP);
  //pinMode(23, INPUT_PULLUP);

  // Turn on LED. Pin 13 has an LED connected on most Arduino boards.
  int led = 13;
  pinMode(led, OUTPUT);
  digitalWrite(led, HIGH);  
}

//---------------------------------------------------------------------------

inline void serial1Rx() {
  #ifdef UART_PASSTHROUGH
  while (Serial1.available()) {      
    Serial.write(Serial1.read());
  }
  #endif
}

//right( &Serial3, 14, 15, 16 ), // Serial and Hall sensors pins for right motor
//left( &Serial1, 18, 19, 20 ) { // Serial and Hall sensors pins for left motor

void uartPassthrough() {
  /*
  Hall rightHall( 14, 15, 16  );
  Hall leftHall( 18, 19, 20 );
  int cnt= 0;
  int printCnt = 0;
  int speed=0;
  */
    
  while( 1 ) {
    
    // If anything comes in Serial1, read it and write it to USB
    while (Serial1.available()) {      
      Serial.write(Serial1.read());
    }

    // If anything comes in USB, read it and write it to Serial1
    while (Serial.available()) {    
      Serial1.write(Serial.read());
    }
/*
    if( ++printCnt > 12 ) //12
    {
      printCnt=0;
      cnt++;
      Serial.printf( "cnt=%d, ticks=%d/%d, bad=%d/%d\n", cnt, rightHall.ticks(), leftHall.ticks(), rightHall.badState(), leftHall.badState() );
    }

  //  serialWriteFrame( &Serial1, -300 ); 
    //delay( 5 );
    
    cnt++;
    if( cnt%10 == 0 && cnt < 3000 ) {
      if( cnt < 400 ) {
        speed -= 10;        
      }
      else if( cnt < 800 ) {
        speed += 10;
      }
      else if( cnt < 1200 ) {
        speed -= 10;        
      }
      else if( cnt < 1600 ) {
        speed += 10;
      }
      else if( cnt < 2000 ) {
        speed -= 10;        
      }
      else if( cnt < 2400 ) {
        speed += 10;
      }
      speed = max( -400, speed );
      speed = min( 0, speed );
      serialWriteFrame( &Serial1, speed );             
    }
    */
  }
}

Machine machine;

void loop()
{
  #ifdef UART_ADAPTER
    uartPassthrough();
  #else
    machine.main();
  #endif
  
  //motor_up_down_test( 200 );
  //hallSensorsTest();
    
  Serial.println( "----------------------------" );
}
