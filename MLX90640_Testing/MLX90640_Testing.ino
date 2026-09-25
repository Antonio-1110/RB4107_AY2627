#include <Wire.h>
#include <Adafruit_MLX90640.h>

Adafruit_MLX90640 mlx;

// Change these to your actual connections
#define SDA_PIN 21
#define SCL_PIN 22

// MLX90640 = 32 x 24 = 768 pixels
float frame[32 * 24];

void setup()
{
    Serial.begin(115200);
    delay(2000);

    Serial.println();
    Serial.println("==============================");
    Serial.println(" MLX90640 Temperature Test");
    Serial.println("==============================");

    // Start I2C
    Wire.begin(SDA_PIN, SCL_PIN);

    // Start conservatively for initial soldering test
    Wire.setClock(100000);

    Serial.println("Connecting to MLX90640...");

    if (!mlx.begin(MLX90640_I2CADDR_DEFAULT, &Wire))
    {
        Serial.println();
        Serial.println("[FAIL] MLX90640 not detected!");
        Serial.println();
        Serial.println("Check:");
        Serial.println(" - 3.3V power");
        Serial.println(" - GND");
        Serial.println(" - SDA");
        Serial.println(" - SCL");
        Serial.println(" - solder bridges");
        Serial.println(" - correct GPIO definitions");

        while (1)
        {
            delay(100);
        }
    }

    Serial.println("[PASS] MLX90640 detected!");

    // Sensor configuration
    mlx.setMode(MLX90640_CHESS);
    mlx.setResolution(MLX90640_ADC_18BIT);
    mlx.setRefreshRate(MLX90640_4_HZ);

    Serial.println("Reading thermal data...");
    Serial.println();
}

void loop()
{
    // Read complete 32 x 24 thermal frame
    if (mlx.getFrame(frame) != 0)
    {
        Serial.println("[ERROR] Failed to read MLX90640 frame.");
        delay(500);
        return;
    }

    float minTemp = frame[0];
    float maxTemp = frame[0];
    float sumTemp = 0;

    int hottestPixel = 0;

    for (int i = 0; i < 768; i++)
    {
        float temp = frame[i];

        sumTemp += temp;

        if (temp < minTemp)
        {
            minTemp = temp;
        }

        if (temp > maxTemp)
        {
            maxTemp = temp;
            hottestPixel = i;
        }
    }

    float averageTemp = sumTemp / 768.0;

    // Approximate centre of the 32 x 24 array
    int centerX = 16;
    int centerY = 12;
    int centerIndex = centerY * 32 + centerX;

    float centerTemp = frame[centerIndex];

    // Location of hottest pixel
    int hotX = hottestPixel % 32;
    int hotY = hottestPixel / 32;

    Serial.println("-------- Thermal Frame --------");

    Serial.print("Minimum:  ");
    Serial.print(minTemp, 1);
    Serial.println(" C");

    Serial.print("Average:  ");
    Serial.print(averageTemp, 1);
    Serial.println(" C");

    Serial.print("Center:   ");
    Serial.print(centerTemp, 1);
    Serial.println(" C");

    Serial.print("Maximum:  ");
    Serial.print(maxTemp, 1);
    Serial.println(" C");

    Serial.print("Hot pixel: (");
    Serial.print(hotX);
    Serial.print(", ");
    Serial.print(hotY);
    Serial.println(")");

    Serial.println();

    delay(500);
}