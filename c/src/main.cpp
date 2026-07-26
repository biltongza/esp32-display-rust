#include <Adafruit_GFX.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "freertos/FreeRTOS.h"
#include "tomthumbmono.h"
#include <atomic>

const uint8_t ROWS = 64;
const uint8_t COLS = 64;
const uint8_t NROWS = ROWS / 2;
const uint8_t PLANES = 6;

const uint8_t LINE1 = ROWS - 1 - 14;
const uint8_t LINE2 = ROWS - 1 - 7;
const uint8_t LINE3 = ROWS - 1;
const uint8_t NBARS = ROWS / 8;

std::atomic<uint32_t> REFRESH_RATE;
std::atomic<uint32_t> RENDER_RATE;
std::atomic<uint32_t> SIMPLE_COUNTER;

MatrixPanel_I2S_DMA *dma_display;
GFXcanvas16 *canvas;

SemaphoreHandle_t mutex;

void print_label(GFXcanvas16 *canvas, uint8_t x, uint8_t y, String label, uint32_t counter)
{
    canvas->setCursor(x, y);
    canvas->printf(label.c_str());
    String str = String(counter);
    int16_t x1, y1;
    uint16_t w, h;
    canvas->getTextBounds(str.c_str(), canvas->getCursorX(), canvas->getCursorY(), &x1, &y1, &w, &h);
    canvas->setCursor(COLS - w, canvas->getCursorY());
    canvas->printf(str.c_str());
}

void render_loop(void *pvParameters)
{
    auto start = xTaskGetTickCount();
    uint8_t counter = 0;
    while (true)
    {
        if (xSemaphoreTake(mutex, 10))
        {
            canvas->fillScreen(0);
            const uint8_t STEP = (256 / COLS);
            for (uint8_t x = 0; x < COLS; x++)
            {
                auto brightness = x * STEP;
                for (uint8_t y = 0; y < NBARS; y++)
                {
                    canvas->drawPixel(x, y, MatrixPanel_I2S_DMA::color565(brightness, 0, 0));
                    canvas->drawPixel(x, y + NBARS, MatrixPanel_I2S_DMA::color565(0, brightness, 0));
                    canvas->drawPixel(x, y + 2 * NBARS, MatrixPanel_I2S_DMA::color565(0, 0, brightness));
                }
            }

            canvas->setTextColor(MatrixPanel_I2S_DMA::color565(255, 255, 0));
            print_label(canvas, 0, LINE3, "Refresh:", REFRESH_RATE.load(std::memory_order::memory_order_relaxed));
            print_label(canvas, 0, LINE2, "Render:", RENDER_RATE.load(std::memory_order::memory_order_relaxed));
            print_label(canvas, 0, LINE1, "Simple:", SIMPLE_COUNTER.load(std::memory_order::memory_order_relaxed));

            xSemaphoreGive(mutex);
        }

        counter++;
        if (xTaskGetTickCount() - start > pdMS_TO_TICKS(1000))
        {
            RENDER_RATE.store(counter, std::memory_order::memory_order_relaxed);
            counter = 0;
            start = xTaskGetTickCount();
        }
        xTaskDelayUntil(&start, 1);
    }
    vTaskDelete(NULL);
}

void display_loop(void *pvParameters)
{
    auto start = xTaskGetTickCount();
    uint8_t counter = 0;
    while (true)
    {
        if (xSemaphoreTake(mutex, 10))
        {
            dma_display->drawRGBBitmap(0, 0, canvas->getBuffer(), canvas->width(), canvas->height());
            xSemaphoreGive(mutex);
        }
        counter++;
        if (xTaskGetTickCount() - start > pdMS_TO_TICKS(1000))
        {
            REFRESH_RATE.store(counter, std::memory_order::memory_order_relaxed);
            counter = 0;
            start = xTaskGetTickCount();
        }
        xTaskDelayUntil(&start, 1);
    }
    vTaskDelete(NULL);
}

void counter_loop(void *pvParameters)
{
    auto start = xTaskGetTickCount();
    while (true)
    {
        if (SIMPLE_COUNTER.fetch_add(1, std::memory_order::memory_order_relaxed) >= 99999)
        {
            SIMPLE_COUNTER.store(0, std::memory_order::memory_order_relaxed);
        }
        xTaskDelayUntil(&start, pdMS_TO_TICKS(100));
    }
}

void setup()
{
    HUB75_I2S_CFG mxconfig(
        COLS, // module width
        ROWS, // module height
        1     // Chain length
    );
    mxconfig.gpio.e = 18;
    mxconfig.clkphase = false;
    mxconfig.driver = HUB75_I2S_CFG::FM6126A;

    mutex = xSemaphoreCreateMutex();

    dma_display = new MatrixPanel_I2S_DMA(mxconfig);
    dma_display->begin();
    canvas = new GFXcanvas16(COLS, ROWS);
    canvas->setTextSize(1);
    canvas->setTextWrap(false);
    canvas->setFont(&TomThumbMono);

    xTaskCreatePinnedToCore(render_loop, "render_loop", 8192, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(display_loop, "display_loop", 8192, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(counter_loop, "counter_loop", 8192, NULL, 5, NULL, 1);
}

void loop()
{
}