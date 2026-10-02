/**
  ******************************************************************************
  * @file    bearing_cnn_data_params.h
  * @author  AST Embedded Analytics Research Platform
  * @date    2026-09-29T16:52:19+0800
  * @brief   AI Tool Automatic Code Generator for Embedded NN computing
  ******************************************************************************
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  ******************************************************************************
  */

#ifndef BEARING_CNN_DATA_PARAMS_H
#define BEARING_CNN_DATA_PARAMS_H

#include "ai_platform.h"

/*
#define AI_BEARING_CNN_DATA_WEIGHTS_PARAMS \
  (AI_HANDLE_PTR(&ai_bearing_cnn_data_weights_params[1]))
*/

#define AI_BEARING_CNN_DATA_CONFIG               (NULL)


#define AI_BEARING_CNN_DATA_ACTIVATIONS_SIZES \
  { 20864, }
#define AI_BEARING_CNN_DATA_ACTIVATIONS_SIZE     (20864)
#define AI_BEARING_CNN_DATA_ACTIVATIONS_COUNT    (1)
#define AI_BEARING_CNN_DATA_ACTIVATION_1_SIZE    (20864)



#define AI_BEARING_CNN_DATA_WEIGHTS_SIZES \
  { 103056, }
#define AI_BEARING_CNN_DATA_WEIGHTS_SIZE         (103056)
#define AI_BEARING_CNN_DATA_WEIGHTS_COUNT        (1)
#define AI_BEARING_CNN_DATA_WEIGHT_1_SIZE        (103056)



#define AI_BEARING_CNN_DATA_ACTIVATIONS_TABLE_GET() \
  (&g_bearing_cnn_activations_table[1])

extern ai_handle g_bearing_cnn_activations_table[1 + 2];



#define AI_BEARING_CNN_DATA_WEIGHTS_TABLE_GET() \
  (&g_bearing_cnn_weights_table[1])

extern ai_handle g_bearing_cnn_weights_table[1 + 2];


#endif    /* BEARING_CNN_DATA_PARAMS_H */
