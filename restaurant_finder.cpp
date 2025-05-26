#include <Arduino.h>
#include <TouchScreen.h>
// core graphics library (written by Adafruit)
#include <Adafruit_GFX.h>

// Hardware-specific graphics library for MCU Friend 3.5" TFT LCD shield
#include <MCUFRIEND_kbv.h>

// LCD and SD card will communicate using the Serial Peripheral Interface (SPI)
// e.g., SPI is used to display images stored on the SD card
#include <SPI.h>

// needed for reading/writing to SD card
#include <SD.h>
// for abs()
#include <stdlib.h>

#include "lcd_image.h"

#define REST_START_BLOCK 4000000
#define NUM_RESTAURANTS 1066
// touch screen pins, obtained from the documentaion
#define YP A3  // must be an analog pin, use "An" notation!
#define XM A2  // must be an analog pin, use "An" notation!
#define YM 9   // can be a digital pin
#define XP 8   // can be a digital pin

#define SD_CS 10
#define JOY_VERT  A9  // should connect A9 to pin VRx
#define JOY_HORIZ A8  // should connect A8 to pin VRy
#define JOY_SEL   53

MCUFRIEND_kbv tft;

#define DISPLAY_WIDTH  480
#define DISPLAY_HEIGHT 320
#define YEG_SIZE 2048

lcd_image_t yegImage = { "yeg-big.lcd", YEG_SIZE, YEG_SIZE };

#define JOY_CENTER   512
#define JOY_DEADZONE 64
#define CURSOR_SIZE 9

// calibration data for the touch screen, obtained from documentation
// the minimum/maximum possible readings from the touch point
#define TS_MINX 100
#define TS_MINY 120
#define TS_MAXX 940
#define TS_MAXY 920

// thresholds to determine if there was a touch
#define MINPRESSURE   10
#define MAXPRESSURE 1000
// This is convert the lat/lon to x/y
#define  MAP_WIDTH  2048
#define  MAP_HEIGHT 2048
#define  LAT_NORTH  5361858l
#define  LAT_SOUTH  5340953l
#define  LON_WEST  -11368652l
#define  LON_EAST  -11333496l

TouchScreen ts = TouchScreen(XP, YP, XM, YM, 300);
int restDistIndex = 0;
// different than SD
Sd2Card card;

struct restaurant {  // 64 Bytes
  int32_t lat;
  int32_t lon;
  uint8_t rating;  // from 0 to 10
  char name[55];
};

struct RestDist {
  uint16_t index;
  uint16_t dist;
};

struct RestDist rest_dist[NUM_RESTAURANTS];
// the cursor position on the display
int cursorX, cursorY;
// map drawing coordinates
int yegMiddleX = YEG_SIZE/2 - (DISPLAY_WIDTH)/2;
int yegMiddleY = YEG_SIZE/2 - DISPLAY_HEIGHT/2;
restaurant prevBlock[8];
uint32_t prevBlockNum = 0;
uint8_t currentRating = 1;
// 0 is qsort, 1 is isort, 2 is both
uint8_t currentSortMethod = 0;
char isorttext[] = "ISORT";
char qsorttext[] = "QSORT";
char bothtext[] = "BOTH";
// forward declaration for rating conversion
uint8_t rating(uint8_t rating);
// forward declaration for redrawing the cursor
void redrawCursor(uint16_t colour);
//  These  functions  convert  between lat/lon map  position  and  x/y
int16_t  lon_to_x(int32_t  lon) {
  return  map(lon , LON_WEST , LON_EAST , 0, MAP_WIDTH);
}
int16_t  lat_to_y(int32_t  lat) {
  return  map(lat , LAT_NORTH , LAT_SOUTH , 0, MAP_HEIGHT);
}

void setup() {
  init();

  Serial.begin(9600);

  pinMode(JOY_SEL, INPUT_PULLUP);

  //    tft.reset();             // hardware reset
  uint16_t ID = tft.readID();      // read ID from display
  Serial.print("ID = 0x");
  Serial.println(ID, HEX);
  if (ID == 0xD3D3) ID = 0x9481;   // write-only shield

  // must come before SD.begin() ...
  tft.begin(ID);                   // LCD gets ready to work

  Serial.print("Initializing SD card...");
  if (!SD.begin(SD_CS)) {
    Serial.println("failed! Is it inserted properly?");
    while (true) {}
  }
  Serial.println("OK!");

  Serial.print("Initializing SPI communication for raw reads...");
  if (!card.init(SPI_HALF_SPEED, SD_CS)) {
    Serial.println("failed! Is the card inserted properly?");
    while (true) {}
  }
  Serial.println("OK");
  tft.setRotation(1);

  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_BLACK);
  tft.setTextSize(2);
  // printing rating selector button
  tft.drawRect(420, 0, 60, 160, TFT_RED);
  tft.fillRect(421, 1, 58, 158, TFT_WHITE);
  tft.setCursor(DISPLAY_WIDTH - 35, 72);
  tft.print(currentRating);
  // printing sort selector button
  tft.drawRect(420, 160, 60, 160, TFT_GREEN);
  tft.fillRect(421, 161, 58, 158, TFT_WHITE);
  tft.setCursor(DISPLAY_WIDTH - 35, 200);
  for (int i = 0; i < 5; i++) {
    tft.setCursor(DISPLAY_WIDTH - 35, tft.getCursorY());
    tft.println(qsorttext[i]);
  }
  // draws the centre of the Edmonton map, leaving the rightmost 60 columns black

  lcd_image_draw(&yegImage, &tft, yegMiddleX, yegMiddleY,
                 0, 0, DISPLAY_WIDTH - 60, DISPLAY_HEIGHT);

  // initial cursor position is the middle of the screen
  cursorX = (DISPLAY_WIDTH - 60)/2;
  cursorY = DISPLAY_HEIGHT/2;

  redrawCursor(TFT_RED);
}
// swap() swaps the memory location that a and b are pointing at
void swap(RestDist* a, RestDist*  b) {
  RestDist c = *a;
  *a = *b;
  *b = c;
}
// redraws a red square cursor of size CURSOR_SIZE at the position
// specified by cursorX and cursorY
void redrawCursor(uint16_t colour) {
  tft.fillRect(cursorX - CURSOR_SIZE/2, cursorY - CURSOR_SIZE/2,
               CURSOR_SIZE, CURSOR_SIZE, colour);
}
// newMap() will draw a new patch of the Edmonton map depending on the
// direction variable "dir" that will vary upon the edge of the screen the cursor hits
void newMap(int dir) {
  switch (dir) {
    // if the right edge is hit
    case (1):
      yegMiddleX += DISPLAY_WIDTH - 60;
      break;
    // if the top edge is hit
    case (2):
      yegMiddleY -= DISPLAY_HEIGHT;
      break;
    // if the left edge is hit
    case (3):
      yegMiddleX -= DISPLAY_WIDTH - 60;
      break;
    // if the bottom edge is hit
    case (4):
      yegMiddleY += DISPLAY_HEIGHT;
      break;
  }
  // reset the cursor to the middle of the screen
  cursorX = (DISPLAY_WIDTH-60)/2;
  cursorY = DISPLAY_HEIGHT/2;

  yegMiddleX = constrain(yegMiddleX, 0, YEG_SIZE - DISPLAY_WIDTH + 60);
  yegMiddleY = constrain(yegMiddleY, 0, YEG_SIZE - DISPLAY_HEIGHT);
  // draw the patch of map
  lcd_image_draw(&yegImage, &tft, yegMiddleX, yegMiddleY,
                 0, 0, DISPLAY_WIDTH - 60, DISPLAY_HEIGHT);
  redrawCursor(TFT_RED);
}
// isort() uses the insertion sort algorithm in the assignment description to
// sort the restaurants using their manhattan distance to the cursor
void isort(RestDist* ptr, int n) {
  int i = 1;
  while (i < n) {
    int j = i;
      while ((j > 0) & (ptr[j-1].dist > ptr[j].dist)) {
        swap(&ptr[j], &ptr[j-1]);
        j--;
      }
    i++;
  }
}
// partition() rearranges the elements of the array so that
// for a chosen pivot, every element to the left of the pivot
// is less than the pivot, and every element to the right of the pivot
// is greater than the pivot. It then returns the position of the pivot
int partition(RestDist* ptr, int low, int high) {
  int pivot = ptr[high].dist;
  int i = low-1;
  for (int j = low; j <= high-1 ; j++) {
    if (ptr[j].dist < pivot) {
      i++;
      swap(&ptr[j], &ptr[i]);
    }
  }
  swap(&ptr[i + 1], &ptr[high]);
  return (i + 1);
}
// qsort() calls partition() recursively, using the Divide and Conquer algorithm
// to sort the array in ascending order.
void qsort(RestDist* ptr, int low, int high) {
	if (low < high) {
		int part = partition(ptr, low, high);
		qsort(ptr, low, part-1);
		qsort(ptr, part+1, high);
	}
}

void getRestaurantFast(int restIndex, restaurant* restPtr) {
  uint32_t blockNum = REST_START_BLOCK + restIndex/8;
  // This is the mechanism to determine whether or not to read in a new block of data
  if (prevBlockNum == blockNum) {
    // If the new requested restarant is in the same block as the previously requested
    // one, then just get the restaurant from the previous data in prevBlock
    *restPtr = prevBlock[restIndex % 8];
    // else you need to read in a new block of data and save it
  } else {
    restaurant restBlock[8];
    while (!card.readBlock(blockNum, (uint8_t*) restBlock)) {
    Serial.println("Read block failed, trying again.");
    }
    // The block that was just read is saved at the prevBlock address
    for (int i = 0; i < 8; i++) {
      prevBlock[i] = restBlock[i];
    }
    // The new block number is saved and the restaurant is saved at restPtr
    prevBlockNum = blockNum;
    *restPtr = restBlock[restIndex % 8];
  }
}
// manDist() gets the manhattan distance of the restaurants in rest_dist of which
// the rating is greater or equal to that of the current rating selected.
void manDist(struct RestDist rest_dist[]) {
  // read in the info of the restaurants from the SD card
  // then change the value of dist to the manhanttan distance from the cursor
  // and the value of index to the index of that restaurant
  restDistIndex = 0;
  for (int i = 0; i < NUM_RESTAURANTS; i++) {
    restaurant rest;
    getRestaurantFast(i, &rest);
    if (rating(rest.rating) >= currentRating) {
      rest_dist[restDistIndex].dist = abs(cursorX + yegMiddleX - lon_to_x(rest.lon)) +
                        abs(cursorY + yegMiddleY - lat_to_y(rest.lat));
      rest_dist[restDistIndex].index = i;
      restDistIndex++;
    }
  }
}

void setting(int n, char s1[], char s2[], int dir) {
  // size is the distance between each section of restaurant's name
  // setting() will set the s1 to have text color black on a white background (highlighted)
  // and s2 to have text color white on a black background (not highlighted)
  // s1 is the name of the newly selected restaurant
  // s2 is the name of the previously selected restaurant
  int size = 15;
  if (dir == 1) {
    tft.fillRect(0, (n - 1)*size, DISPLAY_WIDTH, size, TFT_BLACK);
    tft.setCursor(0, size*((n - 1) % 21));
    tft.setTextColor(0xFFFF, 0x0000);
    tft.print(s1);
    tft.setTextColor(0x0000, 0xFFFF);
    tft.setCursor(0, size*(n % 21));
    tft.print(s2);
  } else {
    tft.fillRect(0, (n + 1)*size, DISPLAY_WIDTH, size, TFT_BLACK);
    tft.setCursor(0, size*((n + 1) % 21));
    tft.setTextColor(0xFFFF, 0x0000);
    tft.print(s2);
    tft.setTextColor(0x0000, 0xFFFF);
    tft.setCursor(0, size*(n % 21));
    tft.print(s1);
  }
}
// mode1() allows us to scroll through a list of 21 restaurant
// it will enter a while loop unless the user clicks the button,
// upon which it enters Mode 0.
int mode1() {
  // This part is just the code from the assignment description
  tft.fillScreen(0);
  tft.setCursor(0, 0);
  tft.setTextWrap(false);
  tft.setTextSize(2);

  manDist(rest_dist);
  if (currentSortMethod == 1) {
    Serial.print("Insertion sort running time: ");
    int isortStart = millis();
    isort(rest_dist, restDistIndex);
    int isortTime = millis() - isortStart;
    Serial.print(isortTime);
    Serial.println(" ms");
  } else if (currentSortMethod == 0) {
    Serial.print("Quick sort running time: ");
    int qsortStart = millis();
    qsort(rest_dist, 0, restDistIndex - 1);
    int qsortTime = millis() - qsortStart;
    Serial.print(qsortTime);
    Serial.println(" ms");
  } else {
    Serial.print("Quick sort running time: ");
    int qsortStart = millis();
    qsort(rest_dist, 0, restDistIndex - 1);
    int qsortTime = millis() - qsortStart;
    Serial.print(qsortTime);
    Serial.println(" ms");
    manDist(rest_dist);

    Serial.print("Insertion sort running time: ");
    int isortStart = millis();
    isort(rest_dist, restDistIndex);
    int isortTime = millis() - isortStart;
    Serial.print(isortTime);
    Serial.println(" ms");
  }
  int32_t selectedRest = 0;
  for (int16_t i = 0; i < 21; i++) {
    restaurant r;
    getRestaurantFast(rest_dist[i].index, &r);
    if (i != selectedRest) {
      tft.setTextColor(0xFFFF, 0x0000);
    } else {
      tft.setTextColor(0x0000, 0xFFFF);
    }
    tft.print(r.name);
    tft.setCursor(0, (i+1)*15);
  }
  tft.print("\n");
  // This while loop is here so that you can't leave the menu
  // unless you pick something
  // the first page is page 0
  int page = 0;
  // the first restaurant of a page has index page*21
  // the last restaurant of a page has index page*21+20
  while (true) {
  	tft.setTextWrap(false);
    int yVal = analogRead(JOY_VERT);
    bool newPage = false;
    restaurant r1, r2;
    if (selectedRest > 20 && page < restDistIndex/21) {
      // reset selectedRest to 0 upon entering a new page
      selectedRest = 0;
      page++;
      newPage = true;
      tft.fillScreen(TFT_BLACK);
      tft.setCursor(0, 0);
      for (int16_t i = page*21; (i < page*21 + 21) && i < restDistIndex; i++) {
        restaurant r;
        getRestaurantFast(rest_dist[i].index, &r);
        if (i != selectedRest + page*21) {
          tft.setTextColor(0xFFFF, 0x0000);
        } else {
          tft.setTextColor(0x0000, 0xFFFF);
        }
        tft.print(r.name);
        tft.setCursor(0, ((i + 1) % 21)*15);
      }
      tft.print("\n");
    } else if (selectedRest < 0 && page != 0) {
      page--;
      selectedRest = 20;
      newPage = true;
      tft.fillScreen(TFT_BLACK);
      tft.setCursor(0, 0);
      for (int16_t i = page*21; i < page*21 + 21; i++) {
          restaurant r;
          getRestaurantFast(rest_dist[i].index, &r);
          if (i != selectedRest + page*21) {
            tft.setTextColor(0xFFFF, 0x0000);
          } else {
            tft.setTextColor(0x0000, 0xFFFF);
          }
          tft.print(r.name);
          tft.setCursor(0, (((i+1))%21)*15);
        }
        tft.print("\n");
    }
    // get the info of the currently (r1) and previously (r2) selected
    // restaurants then change their text color and background
    // using setting()
    if ((yVal < 80) && (selectedRest > -1) && !newPage) {
      selectedRest--;
      if (selectedRest > -1) {
        getRestaurantFast(rest_dist[selectedRest + page*21].index, &r1);
        getRestaurantFast(rest_dist[selectedRest + page*21 + 1].index, &r2);
        setting(selectedRest, r1.name, r2.name, 0);
      }
    } else if ((yVal > 950) && (selectedRest < 21) && (selectedRest + page*21 < restDistIndex - 1) && !newPage) {
      selectedRest++;
      if (selectedRest < 21) {
        getRestaurantFast(rest_dist[selectedRest + 21*(page) - 1].index, &r1);
   	    getRestaurantFast(rest_dist[21*page + selectedRest].index, &r2);
        setting(selectedRest, r1.name, r2.name, 1);
      }
    // if the user clicks the button, returns the selected restaurant
    // which is the index (not the index in struct) in the array rest_dist[]
    // of the selected restaurant
    } else if (digitalRead(JOY_SEL) == LOW) {
      return (selectedRest + 21*page);
    }
  }
}
uint8_t rating(uint8_t rating) {
  int floor = (rating+1)/2;
  return max(floor, 1);
}
// 0 is Rating Selector
// 1 is Sort Selecter
int buttonSelected() {
  TSPoint touch = ts.getPoint();
  // restore pinMode to output after reading the touch
  // this is necessary to talk to tft display
  pinMode(YP, OUTPUT);
  pinMode(XM, OUTPUT);

  if (touch.z < MINPRESSURE || touch.z > MAXPRESSURE) {
    return -1;
  }

  int16_t screen_x = map(touch.y, TS_MINX, TS_MAXX, tft.width() - 1, 0);
  int16_t screen_y = map(touch.x, TS_MINY, TS_MAXY, tft.height() - 1, 0);
  // determine which button is selected
  if (screen_x > 420) {
    if (screen_y < 160 && screen_y > 0) {
      return 0;
    } else if (screen_y > 160 && screen_y < 320) {
      return 1;
    }
  }
  return -1;  // No button was selected
  delay(200);
}
// drawDot() draws a circle dot at the specified lon/lat location
void drawDot(int32_t lat, int32_t lon) {
  // x has to be on the display if I want to draw it
  int y_adjust = lat_to_y(lat) - yegMiddleY;
  int x_adjust = lon_to_x(lon) - yegMiddleX;
  // If the resturant is not on the screen, don't bother trying to draw it
  // I didn't really need the last two conditions but it seems to make it draw
  // quicker if they're there.
  if ((x_adjust > 0) && (y_adjust > 0) && (x_adjust < DISPLAY_WIDTH - 60)
      && (y_adjust < DISPLAY_HEIGHT)) {
    tft.fillCircle(x_adjust, y_adjust, 3, TFT_BLUE);
  }
}
// drawRest() uses drawDot() to draw all the restaurants visible on the screen
void drawRest() {
  for (int i = 0; i < NUM_RESTAURANTS; i++) {
    restaurant dot;
    getRestaurantFast(i, &dot);
    if (rating(dot.rating) >= currentRating) {
      drawDot(dot.lat, dot.lon);
    }
  }
}

void drawRatingButton() {
  tft.fillRect(421, 1, 58, 158, TFT_WHITE);
  tft.setTextColor(TFT_BLACK);
  tft.setTextSize(2);
  // printing rating selector button
  tft.setCursor(DISPLAY_WIDTH-35, 72);
  tft.print(currentRating);
  Serial.print("Rating selected is: ");
  Serial.println(currentRating);
}

void drawSortButton() {
  tft.fillRect(421, 161, 58, 158, TFT_WHITE);
  tft.setTextColor(TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(DISPLAY_WIDTH - 35, 200);
  Serial.print("Doing: ");
  if (currentSortMethod == 2) {
    for (int i = 0; i < 4; i++) {
      tft.setCursor(DISPLAY_WIDTH - 35, tft.getCursorY());
      tft.println(bothtext[i]);
      Serial.print(bothtext[i]);
    }
  } else {
    for (int i = 0; i < 5; i++) {
      tft.setCursor(DISPLAY_WIDTH - 35, tft.getCursorY());
      if (currentSortMethod == 0) {
        tft.println(qsorttext[i]);
        Serial.print(qsorttext[i]);
      } else if (currentSortMethod == 1) {
        tft.println(isorttext[i]);
        Serial.print(isorttext[i]);
      }
    }
  }
  Serial.println();
}
// buttonClick() is responsible for redrawing the patch with the selected restaurant at the center.
// It also takes care of the two boundary cases specified in the assignment description.
void buttonclick() {
  int selectedRest = mode1();
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2);

  restaurant R_SEL;
  getRestaurantFast(rest_dist[selectedRest].index, &R_SEL);
  Serial.println();
  Serial.println(R_SEL.name);
  int Rx = lon_to_x(R_SEL.lon);
  int Ry = lat_to_y(R_SEL.lat);
  // if the x coordinate of the restaurant is out of bound to the right
  if (Rx > YEG_SIZE) {
    yegMiddleX = YEG_SIZE - DISPLAY_WIDTH + 60;
    cursorX = DISPLAY_WIDTH - 60;
  // if the x cooridnate of the restaurant is out of bound to the left
  } else if (Rx < 0) {
    yegMiddleX = 0;
    cursorX = 0;
  // if the restaurant cannot be drawn at the center
  // because its x coordinate surpasses the verticle middle line of the screen to the right
  } else if (Rx + (DISPLAY_WIDTH - 60)/2 > YEG_SIZE) {
    yegMiddleX = YEG_SIZE - DISPLAY_WIDTH + 60;
    cursorX = Rx - yegMiddleX;
  // if the restaurant cannot be drawn at the center
  // because its x coordinate surpasses the verticle middle line of the screen to the left
  } else if (Rx - (DISPLAY_WIDTH - 60)/2 < 0) {
    yegMiddleX = 0;
    cursorX = Rx;
  // the ordinary case
  } else {
    yegMiddleX = Rx - (DISPLAY_WIDTH - 60)/2;
    cursorX = (DISPLAY_WIDTH - 60)/2;
  }
  // if the y coordinate of the restaurant is out of bound to the bottom
  if (Ry > YEG_SIZE) {
    yegMiddleY = YEG_SIZE - DISPLAY_HEIGHT;
    cursorY = DISPLAY_HEIGHT;
  // if the y coordinate of the restaurant is out of bound to the top
  } else if (Ry < 0) {
    yegMiddleY = 0;
    cursorY = 0;
  // if the restaurant cannot be drawn at the center
  // because its y coordinate surpasses the horizontal middle line of the screen to the bottom
  } else if (Ry + DISPLAY_HEIGHT/2 > YEG_SIZE) {
    yegMiddleY = YEG_SIZE - DISPLAY_HEIGHT;
    cursorY = Ry - yegMiddleY;
  // if the restaurant cannot be drawn at the center
  // because its y coordinates surpasses the horizontal middle line of the screen to the top
  } else if (Ry - DISPLAY_HEIGHT/2 < 0) {
    yegMiddleY = 0;
    cursorY = Ry;
  // the ordinary case
  } else {
    yegMiddleY = Ry - DISPLAY_HEIGHT/2;
    cursorY = DISPLAY_HEIGHT/2;
  }

  lcd_image_draw(&yegImage, &tft, yegMiddleX, yegMiddleY,
                 0, 0, DISPLAY_WIDTH - 60, DISPLAY_HEIGHT);
  drawRatingButton();
  drawSortButton();
}
// mode0() allows the user to move around the entire map of Edmonton
void mode0() {
  bool isShift = false;
  int xVal = analogRead(JOY_HORIZ);
  int yVal = analogRead(JOY_VERT);
  TSPoint touch = ts.getPoint();
  pinMode(YP, OUTPUT);
  pinMode(XM, OUTPUT);
  int oldCursorX = cursorX;
  int oldCursorY = cursorY;

  // This is to check if the joystick was clicked
  if (digitalRead(JOY_SEL) == LOW) {
  	buttonclick();
  	isShift = true;
  }
  // This is to determine if the screen was touched in the area which doesn't include
  // the blank 60 pixels on the right
  if (buttonSelected() == 0) {
    currentRating = currentRating % 5 + 1;
    drawRatingButton();
    delay(200);
  } else if (buttonSelected() == 1) {
    currentSortMethod = (currentSortMethod + 1) % 3;
    drawSortButton();
    delay(200);
  }
  if ((touch.z >= MINPRESSURE && touch.z <= MAXPRESSURE) &&
     (map(touch.y, TS_MINX, TS_MAXX, 0, tft.width()) > 60)) {
    drawRest();
  }
  // This lcd_image_draw exists so that the cursor deosn't leave a black trail but
  // rather leaves the part of the map that was once there. If the joystick hasn't
  // been moved, then there is no reason to redraw the cursor so that's the conditions
  // in the if statment.
  if ((analogRead(JOY_VERT) < JOY_CENTER - JOY_DEADZONE ||
      analogRead(JOY_VERT) > JOY_CENTER + JOY_DEADZONE ||
      analogRead(JOY_HORIZ) < JOY_CENTER - JOY_DEADZONE ||
      analogRead(JOY_HORIZ) > JOY_CENTER + JOY_DEADZONE)) {
  	lcd_image_draw(&yegImage, &tft , yegMiddleX + oldCursorX - CURSOR_SIZE/2,
                yegMiddleY + oldCursorY - CURSOR_SIZE/2, oldCursorX - CURSOR_SIZE/2,
                oldCursorY - CURSOR_SIZE/2, CURSOR_SIZE, CURSOR_SIZE);
  }

  // now move the cursor
  // I made it so that the change in cursor position is a function of the distance
  // between Joystick position and the deadzone. 10 is just a random number I
  // chose for a speed constant

  if (yVal < JOY_CENTER - JOY_DEADZONE) {
    cursorY += (yVal - (JOY_CENTER - JOY_DEADZONE))/20;  // decrease the y coordinate of the cursor
  } else if (yVal > JOY_CENTER + JOY_DEADZONE) {
    cursorY += (yVal - (JOY_CENTER + JOY_DEADZONE))/20;
  }

  // remember the x-reading increases as we push left
  if (xVal > JOY_CENTER + JOY_DEADZONE) {
    cursorX -= (xVal - (JOY_CENTER + JOY_DEADZONE))/20;
  } else if (xVal < JOY_CENTER - JOY_DEADZONE) {
    cursorX -= (xVal - (JOY_CENTER - JOY_DEADZONE))/20;
  }

  // This part is to constrain the values of cursorX and cursorY so that the cursor
  // is restricted to being on the display. The 60 subtraction in cursorX is for the
  // Blackbar on the side and the 1 subtraction is due to the way pixels are counted
  // so the cursor needs to be adjusted for that.
  int dir;
  cursorX = constrain(cursorX, CURSOR_SIZE/2,
            DISPLAY_WIDTH - 61 - CURSOR_SIZE/2);
  cursorY = constrain(cursorY, CURSOR_SIZE/2,
            DISPLAY_HEIGHT - CURSOR_SIZE/2);

  // This is here so that when the map justs shifted from the buttonClick, this
  // prevents the cursor from being drawn twice
  if (!isShift) {
    redrawCursor(TFT_RED);
  }

  // This is to determine which direction the map needs to be shifted in when the
  // cursor is at the edge of the screen
  if ((CURSOR_SIZE/2 >= cursorX) & !(yegMiddleX == 0)) {
    dir = 3;
    newMap(dir);
  } else if ((cursorX >= DISPLAY_WIDTH - 61 - CURSOR_SIZE/2) & !(yegMiddleX + DISPLAY_WIDTH - 60 == YEG_SIZE)) {
    dir = 1;
    newMap(dir);
  }
  if ((CURSOR_SIZE/2 >= cursorY) & !(yegMiddleY == 0)) {
    dir = 2;
    newMap(dir);
  } else if ((cursorY >= DISPLAY_HEIGHT - CURSOR_SIZE/2) & !(yegMiddleY + DISPLAY_HEIGHT == YEG_SIZE)) {
    dir = 4;
    newMap(dir);
  }
}
// main() initializes setup() puts mode0() in a while loop. If the user clicks the button while in Mode 0
// the program will enter Mode 1 (mode1() function) which is also a while loop
// that only halts if the button is clicked.
int main() {
  setup();
  while (true) {
    mode0();
  }
  Serial.end();
  return 0;
}
