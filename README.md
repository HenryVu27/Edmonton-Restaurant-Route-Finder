# Edmonton Restaurant Route Finder

This project implements an interactive restaurant finder for Edmonton on an Arduino Mega with a TFT display. Users can navigate a map of Edmonton using a joystick, view nearby restaurants, and select one to re-center the map.

## Features

*   **Interactive Map Navigation:** Pan and scroll across a map of Edmonton using a joystick.
*   **Restaurant Proximity Search:** Displays the 21 closest restaurants to the current cursor position based on Manhattan distance.
*   **Restaurant Selection:** Select a restaurant from the list to view its location on the map.
*   **Dynamic Map Rendering:** The map patch is redrawn as the cursor moves, providing a scrolling effect.
*   **SD Card Integration:** Restaurant data is loaded efficiently from an SD card (though not explicitly mentioned, `getRestaurantFast` implies SD card usage for restaurant data, typical for Arduino projects with larger datasets).

## Hardware Requirements

*   Arduino Mega Board (AMG)
*   A-B style USB cable
*   TFT 3.5" (480x320) Display for MEGA 2560
*   Joystick
*   Breadboard
*   Stylus (for TFT interaction, if applicable, though primary input seems to be joystick)
*   SD Card Reader and SD Card (implied for `getRestaurantFast`)

## Wiring Instructions

Connect the joystick to the Arduino Mega as follows:

| Arduino Pin      | Joystick Pin |
| :--------------- | :----------- |
| Analog Pin A8    | VRy          |
| Analog Pin A9    | VRx          |
| Digital Pin 53   | SW           |
| GND              | GND          |
| 5V               | 5V           |

The TFT Display should be connected to the Arduino Mega as per its specific shield or wiring requirements (typically plugs directly onto the Mega).

## Software Requirements

*   Arduino IDE
*   `avr-gcc` toolchain (if using the provided `Makefile`)
*   Serial monitor software (like the one in Arduino IDE, or `serial-mon` script)

## Project Files

*   `restaurant_finder.cpp`: Main C++ source code for the application.
*   `lcd_image.h` & `lcd_image.cpp`: Likely contain data and functions related to the map image.
*   `Makefile`: Used for compiling and uploading the code via the command line.

*(Restaurant data on an SD card is also required for full functionality).*

## Setup and Installation

1.  **Clone the Repository:**
    ```bash
    git clone https://github.com/HenryVu27/Edmonton-Restaurant-Route-Finder.git
    cd Edmonton-Restaurant-Route-Finder
    ```
2.  **Prepare Files:** Ensure the following files are present in a directory named `a1part`:
    *   `restaurant_finder.cpp`
    *   `lcd_image.h`
    *   `lcd_image.cpp`
    *   `Makefile`
3.  **Hardware Connection:** Wire the Arduino, TFT display, and joystick according to the "Wiring Instructions" section.
4.  **SD Card (if applicable):** Prepare an SD card with the necessary restaurant data file(s) that `getRestaurantFast()` expects.

## Running the Project

1.  Compile and upload the code to the Arduino Mega, then open the serial monitor:
    ```bash
    make upload && serial-mon
    ```
    This command uses the provided `Makefile` to build the project and upload it to the connected Arduino board. It then starts a serial monitor to view any debug output.

## How It Works

The application operates in two main modes and utilizes several key functions:

### Modes of Operation:

*   **Mode 0 (Map Navigation):**
    *   The default mode upon startup.
    *   Displays a patch of the Edmonton map.
    *   The user can move a cursor around the map using the joystick (VRx for X-axis, VRy for Y-axis).
    *   When the cursor reaches the edge of the display, `newMap()` is called to "scroll" the map, loading a new patch. Scrolling is clamped to the boundaries of the full Edmonton map image.
    *   Pressing the joystick button (SW) transitions to Mode 1.

*   **Mode 1 (Restaurant List & Selection):**
    *   Entered by pressing the joystick button in Mode 0.
    *   Calculates the Manhattan distance from the current cursor position to all restaurants using `manDist()`.
    *   Sorts the restaurants by this distance in ascending order using `isort()`.
    *   Displays a list of the 21 closest restaurants.
    *   The user can navigate this list using the joystick.
    *   Pressing the joystick button again selects the highlighted restaurant.
    *   The program then calls `buttonClick()` to redraw the map, centering it on the selected restaurant (with boundary considerations), and returns to Mode 0.

### Core Functionality & Key Functions:

*   **Coordinate Conversion:**
    *   `x_to_lon()`, `y_to_lat()`: Convert map pixel coordinates (X, Y) to geographic coordinates (longitude, latitude).
    *   `lon_to_x()`, `lat_to_y()`: Convert geographic coordinates (longitude, latitude) to map pixel coordinates (X, Y). These likely use linear mapping based on the map image's extents.
*   **Initialization (`setup()`):**
    *   Initializes serial communication, the TFT display, and other necessary components.
    *   Loads restaurant data using `getRestaurantFast()`.
    *   Draws the initial central patch of the Edmonton map.
    *   Places the cursor in the middle of the screen.
*   **Map and Cursor Drawing:**
    *   `redrawCursor()`: Draws the cursor at its current `cursorX`, `cursorY` position.
    *   `newMap()`: Handles map scrolling by drawing a new segment of the map when the cursor hits the display edges.
    *   `drawRest()`: Iterates through restaurants and draws those visible on the current map patch.
    *   `drawDot()`: A helper function used by `drawRest()` to draw a small circle representing a restaurant.
*   **Restaurant Data Handling:**
    *   `getRestaurantFast()`: Quickly reads restaurant information (name, latitude, longitude, etc.) from the SD card.
    *   `manDist()`: Calculates the Manhattan distance between the cursor's effective geographic location and each restaurant.
    *   `isort()` & `swap()`: Implements an insertion sort algorithm to sort restaurants by their Manhattan distance to the cursor.
*   **User Interface & Interaction:**
    *   `setting()`: Configures text and background colors for displaying restaurant information.
    *   `buttonClick()`: Handles the logic after a restaurant is selected in Mode 1. It recenters the map on the selected restaurant, respecting map boundaries.
*   **Main Loop (`main()`):**
    *   Calls `setup()` once.
    *   Enters an infinite loop that primarily manages `mode0()`. Transitions between `mode0()` and `mode1()` are handled within these functions based on joystick input.

## Controls

*   **Joystick Up/Down/Left/Right:**
    *   In **Mode 0:** Moves the cursor on the map.
    *   In **Mode 1:** Navigates the list of nearby restaurants.
*   **Joystick Button Press (SW):**
    *   In **Mode 0:** Switches to Mode 1, displaying the list of nearby restaurants.
    *   In **Mode 1:** Selects the highlighted restaurant, re-centers the map on it, and returns to Mode 0.
