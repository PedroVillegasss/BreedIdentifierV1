/* Copyright 2019 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "main_functions.h"

#include "detection_responder.h"
#include "image_provider.h"
#include "model_settings.h"
#include "person_detect_model_data.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_log.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <esp_log.h>
#include "esp_main.h"
#include "esp_system.h"

// Globals, used for compatibility with Arduino-style sketches.
namespace {
const tflite::Model* model = nullptr;
tflite::MicroInterpreter* interpreter = nullptr;
TfLiteTensor* input = nullptr;

// In order to use optimized tensorflow lite kernels, a signed int8_t quantized
// model is preferred over the legacy unsigned model format. This means that
// throughout this project, input images must be converted from unisgned to
// signed format. The easiest and quickest way to convert from unsigned to
// signed 8-bit integers is to subtract 128 from the unsigned value to get a
// signed value.

#ifdef CONFIG_IDF_TARGET_ESP32S3
constexpr int scratchBufSize = 40 * 1024;
#else
constexpr int scratchBufSize = 0;
#endif
// An area of memory to use for input, output, and intermediate arrays.
constexpr int kTensorArenaSize = 81 * 1024 + scratchBufSize + 20000;
static uint8_t *tensor_arena;//[kTensorArenaSize]; // Maybe we should move this to external
}  // namespace

// The name of this function is important for Arduino compatibility.
void setup() {
  // Map the model into a usable data structure. This doesn't involve any
  // copying or parsing, it's a very lightweight operation.
  model = tflite::GetModel(g_person_detect_model_data);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    MicroPrintf("Model provided is schema version %d not equal to supported "
                "version %d.", model->version(), TFLITE_SCHEMA_VERSION);
    return;
  }

  if (tensor_arena == NULL) {
    tensor_arena = (uint8_t *) heap_caps_malloc(kTensorArenaSize, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }
  if (tensor_arena == NULL) {
    printf("Couldn't allocate memory of %d bytes\n", kTensorArenaSize);
    return;
  }

  // Pull in only the operation implementations we need.
  // This relies on a complete list of all the ops needed by this graph.
  // An easier approach is to just use the AllOpsResolver, but this will
  // incur some penalty in code space for op implementations that are not
  // needed by this graph.
  //
  // tflite::AllOpsResolver resolver;
  // NOLINTNEXTLINE(runtime-global-variables)
  static tflite::MicroMutableOpResolver<9> micro_op_resolver;
  micro_op_resolver.AddAveragePool2D();
  micro_op_resolver.AddConv2D();
  micro_op_resolver.AddDepthwiseConv2D();
  micro_op_resolver.AddReshape();
  micro_op_resolver.AddSoftmax();
  micro_op_resolver.AddMaxPool2D(); 
  micro_op_resolver.AddFullyConnected(); 
  micro_op_resolver.AddLogistic(); 
  micro_op_resolver.AddQuantize();

  // Build an interpreter to run the model with.
  // NOLINTNEXTLINE(runtime-global-variables)
  static tflite::MicroInterpreter static_interpreter(
      model, micro_op_resolver, tensor_arena, kTensorArenaSize);
  interpreter = &static_interpreter;

  // Allocate memory from the tensor_arena for the model's tensors.
  TfLiteStatus allocate_status = interpreter->AllocateTensors();
  if (allocate_status != kTfLiteOk) {
    MicroPrintf("AllocateTensors() failed");
    return;
  }

  // Get information about the memory area to use for the model's input.
  input = interpreter->input(0);

#ifndef CLI_ONLY_INFERENCE
  // Initialize Camera
  TfLiteStatus init_status = InitCamera();
  if (init_status != kTfLiteOk) {
    MicroPrintf("InitCamera failed\n");
    return;
  }
#endif
}

#ifndef CLI_ONLY_INFERENCE
// The name of this function is important for Arduino compatibility.
void loop() {
  // Get image from provider.
  if (kTfLiteOk != GetImage(kNumCols, kNumRows, kNumChannels, input->data.int8)) {
    MicroPrintf("Image capture failed.");
  }

  // Run the model on this input and make sure it succeeds.
  if (kTfLiteOk != interpreter->Invoke()) {
    MicroPrintf("Invoke failed.");
  }

  TfLiteTensor* output = interpreter->output(0);

  // Process the inference results.
  int8_t chihuahua_score = output->data.uint8[kChihuahuaIndex];
  int8_t border_collie_score = output->data.uint8[kBorderCollieIndex];

  float chihuahua_score_f =
      (chihuahua_score - output->params.zero_point) * output->params.scale;
  
  float border_collie_score_f =
      (border_collie_score - output->params.zero_point) * output->params.scale;
  
  int chihuahua_score_int = (chihuahua_score_f) * 100 + 0.5;
  int border_collie_score_int = (border_collie_score_f) * 100 + 0.5;

  printf("Chihuahua score = %d%%, float value: %f\n", chihuahua_score_int, chihuahua_score_f);
  printf("Border Collie score = %d%%, float value: %f\n", border_collie_score_int, border_collie_score_f);

  vTaskDelay(1); // to avoid watchdog trigger
}
#endif

#if defined(COLLECT_CPU_STATS)
  long long total_time = 0;
  long long start_time = 0;
  extern long long softmax_total_time;
  extern long long dc_total_time;
  extern long long conv_total_time;
  extern long long fc_total_time;
  extern long long pooling_total_time;
  extern long long add_total_time;
  extern long long mul_total_time;
#endif

void run_inference(void *ptr) {

  long long start_quantize = esp_timer_get_time();

  /* Convert from uint8 picture data to int8 */
  for (int i = 0; i < kNumCols * kNumRows; i++) {
    input->data.int8[i] = ((uint8_t *) ptr)[i] ^ 0x80;
    // printf("%d, ", input->data.int8[i]);
  }

  long long end_quantize = esp_timer_get_time();
  long long quantize_time = end_quantize - start_quantize;

#if defined(COLLECT_CPU_STATS)
  long long start_time = esp_timer_get_time();

#endif
  // Run the model on this input and make sure it succeeds.
  if (kTfLiteOk != interpreter->Invoke()) {
    MicroPrintf("Invoke failed.");
  }


  TfLiteTensor* output = interpreter->output(0);

  // Process the inference results.

  long long start_response_time = esp_timer_get_time();

  int8_t chihuahua_score = output->data.uint8[kChihuahuaIndex];
  int8_t border_collie_score = output->data.uint8[kBorderCollieIndex];

  float chihuahua_score_f =
      (chihuahua_score - output->params.zero_point) * output->params.scale;
  
  float border_collie_score_f =
      (border_collie_score - output->params.zero_point) * output->params.scale;
  
  int chihuahua_score_int = (chihuahua_score_f) * 100 + 0.5;
  int border_collie_score_int = (border_collie_score_f) * 100 + 0.5;

  printf("Chihuahua score = %d%%\n", chihuahua_score_int);
  printf("Border Collie score = %d%%\n", border_collie_score_int);
  
  printf("Cuantización de la imagen = %lld ms\n", quantize_time);  
  long long end_response_time = esp_timer_get_time();
  long long response_time = end_response_time - start_response_time;
  printf("Response time = %lld\n", response_time);


#if defined(COLLECT_CPU_STATS)
  long long total_time = (esp_timer_get_time() - start_time);
  RSR(CCOUNT, ccount_new); 
  unsigned cycles = ccount_new - ccount;
  RSR(ICOUNT, icount);


  long long layers_time = softmax_total_time + fc_total_time + dc_total_time + conv_total_time + pooling_total_time + add_total_time + mul_total_time;
  printf("Total time = %lld ms\n", total_time);
  printf("Softmax time = %lld ms\n", softmax_total_time);
  printf("FC time = %lld ms\n", fc_total_time);
  printf("DC time = %lld ms\n", dc_total_time);
  printf("conv time = %lld ms\n", conv_total_time);
  printf("Pooling time = %lld ms\n", pooling_total_time);
  printf("add time = %lld ms\n", add_total_time);
  printf("mul time = %lld ms\n", mul_total_time);
  printf("Layers time %lld ms\n", layers_time);
  printf("Meassure total time  %lld ms\n", layers_time + quantize_time + response_time);

  /* Reset times */
  total_time = 0;
  softmax_total_time = 0;
  dc_total_time = 0;
  conv_total_time = 0;
  fc_total_time = 0;
  pooling_total_time = 0;
  add_total_time = 0;
  mul_total_time = 0;
#endif
}