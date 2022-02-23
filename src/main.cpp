#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>

#include "MLX90640_API.h"
#include "MLX90640_I2C_Driver.h"

#include "Adafruit_GFX.h"
#include "Adafruit_ILI9341.h"

// For the Firebeetle ESP32.
#define TFT_CS    D9
#define TFT_DC    D7
#define TFT_MOSI  MOSI
#define TFT_CLK   SCK
#define TFT_RST   D6
#define TFT_MISO  MISO
#define TFT_LED   D3

#define PIXEL_SIZE  4

#define TA_SHIFT          8                       // Default shift for MLX90640 in open air
#define ORIGINAL_W_RES    32                      // Width resolution
#define ORIGINAL_H_RES    24                      // Height resolution
#define INTERPOLE_W_RES   (2*(ORIGINAL_W_RES)-1)
#define INTERPOLE_H_RES   (2*(ORIGINAL_H_RES)-1)
#define DELAY             1500

const uint8_t MLX90640_address = 0x33;                                    // Default 7-bit unshifted address of the MLX90640
static float mlx90640To[ORIGINAL_W_RES * ORIGINAL_H_RES] = {0};           // Array of total pixels in MLX90640
static float mlx90640Image[INTERPOLE_W_RES * INTERPOLE_H_RES] = {0};      // Array for the interpolate image

paramsMLX90640 mlx90640;

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_MOSI, TFT_CLK, TFT_RST, TFT_MISO);

uint8_t R_colour = 0, G_colour = 0, B_colour = 0;   // RGB color value
uint32_t k = 0;
float T_max, T_min, vdd;                            // Maximum or minimum measured temperature
float T_center;                                     // Temperature in the center of the screen

// ***************************************
// ************** FUNCTIONS **************
// ***************************************

boolean isConnected();
void getColour(float);

// ***************************************
// **************** SETUP ****************
// ***************************************
void setup() {
  int status = 0x00;

  Wire.begin();
  Wire.setClock(1000000);     // Increase I2C clock speed to 1MHz
  MLX90640_I2CFreqSet(1000);  // 

  // TFT display start
  pinMode(TFT_LED, OUTPUT);
  digitalWrite(TFT_LED, HIGH);

  // Show the debug information on the start
  tft.begin(80000000);      // Increase SPI clock speed to 80MHz
  tft.setRotation(1);
  tft.fillScreen(ILI9341_BLACK);
  tft.setCursor(2, 2);
  tft.setTextSize(2);
  tft.setTextColor(tft.color565(0, 255, 0));  // Lime color

  tft.println("**************************");
  tft.println("        Thermal Cam       ");
  tft.println("**************************");

  uint32_t Freq = getCpuFrequencyMhz();
  tft.print("CPU Freq: ");
  tft.print(Freq);
  tft.println(" MHz");
  Freq = getXtalFrequencyMhz();
  tft.print("XTAL Freq: ");
  tft.print(Freq);
  tft.println(" MHz");
  Freq = getApbFrequency();
  tft.print("APB Freq: ");
  tft.print(Freq);
  tft.println(" Hz");
  tft.println("");
  delay(DELAY);

  while (!Serial);       // Wait for user to open terminal

  status = isConnected();
  if (status == false) {
    uint32_t i = 0;
    if (i < 0) {
      tft.println("MLX90640 not detected at default I2C address. Please check wiring. Freezing.");
      while (!status) {
        i += 1;
        delay(DELAY);
      }
    }
  }

  tft.println("");
  tft.println("MLX90640 online!");
  tft.println("");
  delay(DELAY);

  // Reads all the necessary EEPROM data from the MLX90640
  // it's needs at least 832 words in the MCU to store the data
  uint16_t eeMLX90640[832];

  status = MLX90640_DumpEE(MLX90640_address, eeMLX90640);

  tft.print("System parameters -> ");
  if (status != 0) {
    tft.println("Fail");
  } else {
    tft.println("OK");
  }
    
  // Get the parameters of the MLX90640 to know if there is a error
  status = MLX90640_ExtractParameters(eeMLX90640, &mlx90640);
  tft.print("Params extraction -> ");
  if (status != 0) {
    tft.println("Fail");
    tft.print("Status -> ");
    tft.println(status);
  } else if (status == 0) {
    tft.println("OK");
    delay(DELAY);
  }

  //Once params are extracted, we can release eeMLX90640 array
  MLX90640_I2CWrite(0x33, 0x800D, 6401); // writes the value 1901 (HEX) = 6401 (DEC) in the register at position 0x800D to enable reading out the temperatures!!!
  // ===============================================================================================================================================================
  status = MLX90640_SetRefreshRate(MLX90640_address,  MLX90640_REFRESH_RATE_16HZ);    //Set rate to 64Hz effective - Works
  tft.print("Refresh status    -> ");
  if (status != 0) {
    tft.println("Fail");
    tft.print("Status -> ");
    tft.println(status);
  } else if (status == 0) {
    tft.println("OK");
  }
  delay(DELAY);

  status = MLX90640_GetCurMode(MLX90640_address);
  tft.print("Working mode      -> ");
  switch (status) {
    case 0:
      tft.println("Inter");
      break;
    case 1:
      tft.println("Chess");
      break;
    case -1:
      tft.println("NACK");
      break;
    default:
      tft.println("Diff");
      break;
  }
  delay(DELAY);

  // Draw the GUI for vizualizate the data
  tft.begin(80000000);
  tft.setRotation(1);
  tft.fillScreen(ILI9341_BLACK);
  tft.fillRect(0, 0, 319, 15, tft.color565(255, 0, 10));
  tft.setCursor(130, 5);
  tft.setTextSize(1);
  tft.setTextColor(ILI9341_WHITE, tft.color565(255, 0, 10));
  tft.print("Thermography");

  // White lines for the colour-scale
  for (uint8_t i = 0; i < 210; i += 30) {
    tft.drawLine(255, 210 - i, 267, 210 - i  , tft.color565(255, 255, 255));
  }

  tft.setCursor(40, 220);
  tft.setTextColor(ILI9341_WHITE, tft.color565(0, 0, 0));
  tft.print("Temp: ");

  tft.setCursor(160, 220);
  tft.setTextColor(ILI9341_WHITE, tft.color565(0, 0, 0));
  tft.print("Vin: ");

  // Drawing the colour-scale
  for (size_t i = 0; i < 181; ++i){
    getColour(i);
    tft.drawLine(255, 210 - i, 262, 210 - i, tft.color565(R_colour, G_colour, B_colour));
  }

  tft.fillRect(275, 25, 37, 10, tft.color565(0, 0, 0));
  tft.fillRect(275, 205, 37, 10, tft.color565(0, 0, 0));
  tft.fillRect(115, 220, 37, 10, tft.color565(0, 0, 0));

}

// **********************************
// ************** LOOP **************
// **********************************
void loop() {
  register uint32_t i = 0;
  register uint32_t j = 0;

  for (i = 0; i < 2; ++i) { //Read both subpages
    // Reads all the necessary frame data from MLX90640
    // needs at least 834 words in the memory MCU to stored the data
    uint16_t mlx90640Frame[834] = {0};
    int status = MLX90640_GetFrameData(MLX90640_address, mlx90640Frame);

    if (status < 0) {
      Serial.print("GetFrame Error: ");
      Serial.println(status);
    }

    vdd = MLX90640_GetVdd(mlx90640Frame, &mlx90640);
    float tr = MLX90640_GetTa(mlx90640Frame, &mlx90640) - TA_SHIFT; //Reflected temperature based on the sensor ambient temperature
    float emissivity = 0.95;

    MLX90640_CalculateTo(mlx90640Frame, &mlx90640, emissivity, tr, mlx90640To);
  }

  // Determine T_min and T_max and eliminate error pixels
  mlx90640To[1 * 32 + 21] = 0.5 * (mlx90640To[1 * 32 + 20] + mlx90640To[1 * 32 + 22]); // eliminate the error-pixels
  mlx90640To[4 * 32 + 30] = 0.5 * (mlx90640To[4 * 32 + 29] + mlx90640To[4 * 32 + 31]); // eliminate the error-pixels

  T_min = *mlx90640To;
  T_max = *mlx90640To;

  for (i = (ORIGINAL_W_RES * ORIGINAL_H_RES) - 1; --i;) {
    if ((mlx90640To[i] > -41) && (mlx90640To[i] < 301)) {
      if (mlx90640To[i] < T_min) {
        T_min = mlx90640To[i];
      } else if (mlx90640To[i] > T_max) {
        T_max = mlx90640To[i];
      }
    } else if (i > 0) { // temperature out of range
      mlx90640To[i] = mlx90640To[i - 1];
    } else {
      mlx90640To[i] = mlx90640To[i + 1];
    }

  }

  // Determine T_center
  T_center = mlx90640To[11 * 32 + 15];
  
  // **********************************
  // ***** Bilinear Interpolation *****
  // **********************************
  // https://learn.adafruit.com/improved-amg8833-pygamer-thermal-camera/the-1-2-3s-of-bilinear-interpolation
  // Copy the values from the original MLX90640 array to a bigger array for the interpolation
  for (i = 0; i < ORIGINAL_H_RES; ++i) {
    for (j = 0; j < ORIGINAL_W_RES; ++j) {
      k = 2 * ((i * INTERPOLE_W_RES) + j);

      // First pass
      // Copy the original values into the even positions 
      // for calculate the average between the original values for the odd positions
      if (k % 2 == 0) {
        mlx90640Image[k] = mlx90640To[(i * ORIGINAL_W_RES) + j];
        if (k >= 1 && j > 0){
          mlx90640Image[k - 1] = ((mlx90640Image[k - 2] + mlx90640Image[k]) * (1 / 2.0));
        }
      }
      
      // Second pass
      // Calculated all the average values 
      // for the odd rows using the values of the even
      if (i > 0) {
        if (j < 1) {
          mlx90640Image[k - INTERPOLE_W_RES] = (mlx90640Image[((k - INTERPOLE_W_RES) - INTERPOLE_W_RES)] + mlx90640Image[(((i * 2)  * INTERPOLE_W_RES) + j)]) * (1 / 2.0);
        } else {
          mlx90640Image[k - INTERPOLE_W_RES] = (mlx90640Image[((k - INTERPOLE_W_RES) - INTERPOLE_W_RES)] + mlx90640Image[(((i * 2)  * INTERPOLE_W_RES) + j) + j]) * (1 / 2.0);
          mlx90640Image[(k - INTERPOLE_W_RES) - 1] = (mlx90640Image[(((k - INTERPOLE_W_RES) - INTERPOLE_W_RES)) - 1] + mlx90640Image[(((i * 2)  * INTERPOLE_W_RES) + j) + j - 1]) * (1 / 2.0);
        }
      }
    }
  }

  // Drawing the image
  for (i = 0; i < INTERPOLE_H_RES; ++i) {
    for (j = 0; j < INTERPOLE_W_RES; ++j) {
      mlx90640Image[((i * INTERPOLE_W_RES) + j)] = 180.0 * (mlx90640Image[((i * INTERPOLE_W_RES) + j)] - T_min) * (1 / (T_max - T_min));
      getColour(mlx90640Image[((i * INTERPOLE_W_RES) + j)]);
      tft.fillRect((248 - (j * PIXEL_SIZE)), (25 + (i * PIXEL_SIZE)), PIXEL_SIZE, PIXEL_SIZE, tft.color565(R_colour, G_colour, B_colour));

    }
  }

  // Print the T_max, T_min and the center T values
  tft.setTextColor(ILI9341_WHITE, tft.color565(0, 0, 0));
  tft.setCursor(280, 25);
  tft.print(T_max, 1);
  tft.setCursor(280, 205);
  tft.print(T_min, 1);
  tft.setCursor(80, 220);
  tft.print(T_center, 1);
  tft.setCursor(185, 220);
  tft.print(vdd, 2);

  // Draw a cross in the center
  tft.drawLine(248 - 31*PIXEL_SIZE + 3.5 - 5, 20*PIXEL_SIZE + 35 + 3.5,     248 - 31*PIXEL_SIZE + 3.5 + 5,  20*PIXEL_SIZE + 35 + 3.5,      tft.color565(0, 255, 0));
  tft.drawLine(248 - 31*PIXEL_SIZE + 3.5,     20*PIXEL_SIZE + 35 + 3.5 - 5, 248 - 31*PIXEL_SIZE + 3.5,      20*PIXEL_SIZE + 35 + 3.5 + 5, tft.color565(0, 255, 0));

  // Draw the Celsius symbol side the values
  tft.setCursor(313, 25);
  tft.print("C");
  tft.setCursor(313, 205);
  tft.print("C");
  tft.setCursor(115, 220);
  tft.print("C");
  tft.setCursor(215, 220);
  tft.print("V");
}

// Determine the color
void getColour(float j) {
  if (j >= 0.0 && j < 30.0) {
    R_colour = 0;
    G_colour = 0;
    B_colour = 20.0 + (120.0 * (1 / 30.0)) * j;

  } else if (j >= 30.0 && j < 60.0) {
    R_colour = (120.0 * (1 / 30.0)) * (j - 30.0);
    G_colour = 0;
    B_colour = 140.0 - (60.0 * (1 / 30.0)) * (j - 30.0);

  } else if (j >= 60.0 && j < 90.0) {
    R_colour = 120.0 + (135.0 * (1 / 30.0)) * (j - 60.0);
    G_colour = 0.0;
    B_colour = 80.0 - (70.0 * (1 / 30.0)) * (j - 60.0);

  } else if (j >= 90.0 && j < 120.0) {
    R_colour = 255;
    G_colour = 0.0 + (60.0 * (1 / 30.0)) * (j - 90.0);
    B_colour = 10.0 - (10.0 * (1 / 30.0)) * (j - 90.0);

  } else if (j >= 120.0 && j < 150.0) {
    R_colour = 255;
    G_colour = 60.0 + (175.0 * (1 / 30.0)) * (j - 120.0);
    B_colour = 0;

  } else if (j >= 150.0 && j <= 180.0) {
    R_colour = 255;
    G_colour = 235.0 + (20.0 * (1 / 30.0)) * (j - 150.0);
    B_colour = 0 + 255.0 * (1 / 30.0) * (j - 150.0);

  }
}

// Returns true if the MLX90640 is detected on the I2C bus
boolean isConnected() {
  Wire.beginTransmission((uint8_t)MLX90640_address);

  if (Wire.endTransmission() != 0)
    return false; //Sensor did not ACK

  return true;
}

