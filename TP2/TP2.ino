#include <Wire.h>
#include <cstdlib>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>

// 0x27 est typiquement l'adresse I2C de l'ecran. 
// Si ça ne fonctionne pas, essayez avec 0x3f ou 0x4b

#if defined(ARDUINO) && ARDUINO >= 100
#define printByte(args)  write(args);
#else
#define printByte(args)  print(args,BYTE);
#endif

// Adresse habituelle du ADS7830
#define ADS7830_ADDR 0x4B

// Valeurs de sorties : -1 pour err, sinon entre 0 et 255.
// addr = adresse du module adc
// int channel = le canal de l'adc. A0 = 0, A1 = 1, ...
int ads7830_read_selected_channel(int addr, int channel)
{
	const int read_command_base = 0x84;
	
	// Transmet la commande de sélection de canal
	Wire.beginTransmission(addr);
	{
		// 3 bits dans l'octet de commande sont utilisés pour indiquer le canal de lecture
		// mais le mapping est un peu bizarre :
		// En considérant le nibble suivant : MSB C2 C1 C0
		// C2 = 1
		// C1 = 4
		// C0 = 2
		// Le canal 5 devient : MSB 1 1 0
		// Le canal 3 devient : MSB 1 0 1
		// Le canal 6 devient : MSB 0 1 1
		unsigned int arg_channel = (((channel << 2) | (channel >> 1)) & 0x07) << 4;
		unsigned char cmd = (unsigned char)((read_command_base | arg_channel) & 0xFF);
		Wire.write(cmd);
	}
	Wire.endTransmission();
	
	// Lit la réponse.
	Wire.requestFrom(addr, 1);
	if( !Wire.available() )
	{
		return -1;
	}
	
	return Wire.read();
}

LiquidCrystal_I2C lcd(0x3f, 16, 2);

int buttonPin = 4;
int buzzerPin = 18;

int lightPin = 19;
bool areLightsOn = false;
double lightThreshold = 160;
double lightThresholdLeniance = 20;

int motorForwardPin = 13;
int motorBackwardsPin = 14;

double wheelSpeed = 0;
double maxWheelSpeed = 100;
double minWheelSpeed = -50;

long previousSpeedUpdate = 0;
long speedUpdateTimer = 50;
double forwardSpeedPerSecond = 5;
double backwardSpeedPerSecond = 3;
double dragSpeedPerSecond = 1;

int steeringPin = 26;
Servo steeringServo;
long previousSteeringUpdate = 0;
long steeringUpdateTimer = 250;

int switchPin = 35;
int blinkerPin = 23;
long previouBlinkTime = 0;
long blinkerTimerOff = 250;
long blinkerTimerOn = 500;
bool blinkerState = HIGH;
bool blinkersOn = false;

int tempStrength = 0;

uint8_t lightbulbShape[8] = {
	0b01110, 
	0b10001, 
	0b10101, 
	0b10101, 
	0b01110, 
	0b01110, 
	0b00100, 
	0b00000
} ;

uint8_t arrowbackwardsShape[8] = {
	0b11000, 
	0b01100, 
	0b00110, 
	0b11111, 
	0b11111, 
	0b00110, 
	0b01100, 
	0b11000
} ;

uint8_t arrowForwardsShape[8] = {
	0b00011, 
	0b00110, 
	0b01100, 
	0b11111, 
	0b11111, 
	0b01100, 
	0b00110, 
	0b00011
} ;

void setup() {
	Serial.begin(115200);
	lcd.init(); // Initialize the LCD
	lcd.backlight(); // Turn on the backlight
	lcd.display();
	lcd.clear();

	wheelSpeed = 0;
	
	pinMode(buttonPin, INPUT);
	pinMode(buzzerPin, OUTPUT);
	pinMode(lightPin, OUTPUT);
	pinMode(motorForwardPin, OUTPUT);
	pinMode(motorBackwardsPin, OUTPUT);
	pinMode(steeringPin, OUTPUT);
	pinMode(switchPin, INPUT);
	pinMode(blinkerPin, OUTPUT);

	digitalWrite(buzzerPin, LOW);
  digitalWrite(lightPin, LOW);
	digitalWrite(motorForwardPin, LOW);
	digitalWrite(motorBackwardsPin, LOW);
	digitalWrite(steeringPin, LOW);
	digitalWrite(blinkerPin, LOW);

	steeringServo.attach(steeringPin);

	lcd.createChar(0, lightbulbShape);
	lcd.createChar(1, arrowbackwardsShape);
	lcd.createChar(2, arrowForwardsShape);
	lcd.home(); // Je ne suis pas certain pourquoi, mais dans mes tests il fallait absolument que j'appel cette fonction après avoir définie mes formes.
	tempStrength = 1;
}

void loop() {
	unsigned long currentMillis = millis();
	
	if(currentMillis - previousSteeringUpdate > steeringUpdateTimer) {
    // save the last time you blinked the LED 
    previousSteeringUpdate = currentMillis;   
    handleTurning();
  }
	if(currentMillis - previousSpeedUpdate > speedUpdateTimer) {
		handleAcceleration();
	}
	handleButton();
	handleBlinkers(currentMillis);
	handleLights();
}


void handleAcceleration(){
	double preYStrength = ads7830_read_selected_channel(ADS7830_ADDR, 1);
	double yStrength = (preYStrength/255) - 0.50;
	
	
	if(yStrength >  0.12){
		wheelSpeed += yStrength * forwardSpeedPerSecond * 0.05;
	}else{
		if(yStrength < -0.12){
			wheelSpeed += yStrength * backwardSpeedPerSecond * 0.05;
		}else if(wheelSpeed != 0){
			double decrementValue = (wheelSpeed/abs(wheelSpeed)) * dragSpeedPerSecond * 0.05;
			if((wheelSpeed < 0 && wheelSpeed - decrementValue > 0) || (wheelSpeed > 0 && wheelSpeed - decrementValue < 0)){
				wheelSpeed = 0;
			}else{
				wheelSpeed -= decrementValue;
			}
		}
	}
	
	if(wheelSpeed > maxWheelSpeed) {wheelSpeed = maxWheelSpeed;}
	if(wheelSpeed < minWheelSpeed) {wheelSpeed = minWheelSpeed;}
	if(wheelSpeed > 10){
		analogWrite(motorForwardPin, abs(wheelSpeed*2.55));
		analogWrite(motorBackwardsPin, 0);
	}else if(wheelSpeed < -10){
		analogWrite(motorBackwardsPin, abs(wheelSpeed*2.55));
		analogWrite(motorForwardPin, 0);
	}else{
		analogWrite(motorForwardPin, 0);
		analogWrite(motorBackwardsPin, 0);
	}
}

void handleTurning(){
	double preXStrength = ads7830_read_selected_channel(ADS7830_ADDR, 0);
	double xStrength = abs((preXStrength/255)-1);
	steeringServo.write(xStrength * 180);
	tempStrength = tempStrength + 1;
	lcd.clear();
	if(wheelSpeed > 10){
		lcd.printByte(1);
	}
	if(wheelSpeed < -10){
		lcd.printByte(2);
	}
	lcd.print(abs(wheelSpeed));
	if(blinkersOn){
		lcd.printByte(0);
	}
}	

void handleButton(){
	if(digitalRead(buttonPin) == 0){
		tone(buzzerPin, 1000, 100); // 1 kHz tone
	}else{
		noTone(buzzerPin);
	}
}	

void handleBlinkers(unsigned long currentMillis){
	
	if(digitalRead(switchPin) != LOW){
		blinkersOn = true;
		if(blinkerState == HIGH){
			if(currentMillis - previouBlinkTime > blinkerTimerOn) {
				blinkerState = LOW;
				previouBlinkTime = currentMillis;
			}
		}else{
			if(currentMillis - previouBlinkTime > blinkerTimerOff) {
				blinkerState = HIGH;
				previouBlinkTime = currentMillis;
			}
		}
		digitalWrite(blinkerPin, blinkerState);
	}else{
		blinkersOn = false;
		blinkerState = LOW;
		digitalWrite(blinkerPin, false);
	}
}

void handleLights(){
	double lightStrength = ads7830_read_selected_channel(ADS7830_ADDR, 2);
	Serial.println(lightStrength);
	if(!areLightsOn && lightStrength > lightThreshold + lightThresholdLeniance){
		areLightsOn = true;
	}else if(areLightsOn && lightStrength < lightThreshold - lightThresholdLeniance){
		areLightsOn = false;
	}
	digitalWrite(lightPin, areLightsOn);
}
