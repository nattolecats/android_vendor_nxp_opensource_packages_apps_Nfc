/******************************************************************************
 *
 *  Copyright 2019-2024 NXP
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 ******************************************************************************/

#include "NfcSelfTest.h"
#include "NfcJniUtil.h" // for JNIEnv, jobject & jint
#include "nfc_config.h"
#include <android-base/logging.h>

/* Declaration of the singleTone class(static member) */
NfcSelfTest NfcSelfTest::sSelfTestMgr;

nxp_selftest_data gselfTestData;
extern bool nfc_debug_enabled;
extern SyncEvent sChangeDiscTechEvent;
static SyncEvent sNfaVscNtfEvent;

using android::base::StringPrintf;
using namespace android;

namespace android {
extern SyncEvent gNfaSetConfigEvent;
extern bool nfcManager_isNfcActive();
extern bool isDiscoveryStarted();
extern void startRfDiscovery(bool isStart);
extern int nfcManager_doPartialInitialize(JNIEnv* e, jobject o, jint mode);
extern int nfcManager_doPartialDeInitialize(JNIEnv*, jobject);
extern tNFA_STATUS NxpNfc_Write_Cmd_Common(uint8_t retlen, uint8_t* buffer);
}  // namespace android
tNFC_chipType NFC_GetChipType();

/*******************************************************************************
 ** Set the global Self Test status to @param value
 ** @param status- Status to be set
 ** @return None
 *******************************************************************************/
void SetSelfTestCbStatus(tNFA_STATUS status) { gselfTestData.wstatus = status; }

/*******************************************************************************
 ** Get the global Self Test status
 ** @return tNFA_STATUS
 *******************************************************************************/
tNFA_STATUS GetSelfTestCbStatus(void) { return gselfTestData.wstatus; }

/*******************************************************************************
 ** Initialize member variables.
 ** @return None
 *******************************************************************************/
NfcSelfTest::NfcSelfTest() : SelfTestType(TEST_TYPE_NONE) {}

/*******************************************************************************
 ** Release all resources.
 ** @return None
 *******************************************************************************/
NfcSelfTest::~NfcSelfTest() {}

/*******************************************************************************
 ** Get the NfcSeManager singleton object.
 ** @return NfcSeManager object.
 *******************************************************************************/
NfcSelfTest& NfcSelfTest::GetInstance() { return sSelfTestMgr; }
/*******************************************************************************
 ** Executes: Perform Prbs
 ** @param  on denotes
 **         TRUE  - prbs start()
 **         FALSE - prbs stop()
 ** @return status SUCCESS or FAILED.
 *******************************************************************************/
tNFA_STATUS NfcSelfTest::PerformPrbs(bool on) {
  tNFA_STATUS status = NFA_STATUS_FAILED;
  uint8_t startPrbs[] = {CMD_TYPE_PRBS_ON};
  uint8_t stopPrbs[] = {CMD_TYPE_CORE_RESET, CMD_TYPE_CORE_INIT,
                        CMD_TYPE_NXP_PROP_EXT};

  if (on)
    status = executeCmdSeq(startPrbs, sizeof(startPrbs));
  else
    status = executeCmdSeq(stopPrbs, sizeof(stopPrbs));

  return status;
}
/*******************************************************************************
 ** Function:        NxpResponse_SelfTest_Cb
 **
 ** Description:     Store the value of RF_TRANSITION_CFG and notify the
 **                  Nxp_doResonantFrequency along with updated status
 **
 ** Returns:         void
 *******************************************************************************/
static void NxpResponse_SelfTest_Cb(uint8_t event, uint16_t param_len,
                                    uint8_t* p_param) {
  (void)event;
  LOG(INFO)
      << StringPrintf("%s Received length data = 0x%x status = 0x%x", __func__,
                      param_len, p_param[3]);

  if (NFA_STATUS_OK == p_param[3]) {
    if (gselfTestData.copyData) {
      memcpy((void*)gselfTestData.prestorerftxcfg, (void*)(p_param + 5),
              (size_t)p_param[4]);
      gselfTestData.copyData = false;
    }
    SetSelfTestCbStatus(NFA_STATUS_OK);
  } else {
    SetSelfTestCbStatus(NFA_STATUS_FAILED);
  }

  SyncEventGuard guard(gselfTestData.NxpSelfTestEvt);
  gselfTestData.NxpSelfTestEvt.notifyOne();
}

/*******************************************************************************
 ** Provides the command buffer for the given command type
 ** @param CmdBuf- for the given command type
 **        aType - is the command type
 ** @return length of the command buffer
 *******************************************************************************/
uint8_t NfcSelfTest::GetCmdBuffer(uint8_t* aCmdBuf, uint8_t aType) {
  uint8_t cmdLen = 0;
  static uint8_t rf_tx_cfg_restore[6][4] = {{0}, {0}, {0}, {0}, {0}, {0}};
  /* TID     :    0x60 0x60 0x60 0x12 0x12 0x12
   * CLIF Reg:    0x4E 0x50 0x4F 0x4E 0x4F 0x50
   */

  LOG(INFO) << StringPrintf("Command type is %d", aType);
  switch (aType) {
    case CMD_TYPE_CORE_RESET: {
      uint8_t CMD_CORE_RESET[] = {0x20, 0x00, 0x01, 0x00};
      cmdLen = sizeof(CMD_CORE_RESET);
      memcpy(aCmdBuf, CMD_CORE_RESET, cmdLen);
      break;
    }
    case CMD_TYPE_CORE_INIT: {
      uint8_t CMD_CORE_INIT[] = {0x20, 0x01, 0x02, 0x00, 0x00};
      cmdLen = sizeof(CMD_CORE_INIT);
      memcpy(aCmdBuf, CMD_CORE_INIT, cmdLen);
      break;
    }
    case CMD_TYPE_NXP_PROP_EXT: {
      uint8_t CMD_NXP_PROP_EXT[] = {0x2F, 0x02, 0x00};
      cmdLen = sizeof(CMD_NXP_PROP_EXT);
      memcpy(aCmdBuf, CMD_NXP_PROP_EXT, cmdLen);
      break;
    }
    case CMD_TYPE_NFCC_STANDBY_ON: {
      uint8_t CMD_NFCC_STANDBY_ON[] = {0x2F, 0x00, 0x01, 0x01};
      cmdLen = sizeof(CMD_NFCC_STANDBY_ON);
      memcpy(aCmdBuf, CMD_NFCC_STANDBY_ON, cmdLen);
      break;
    }
    case CMD_TYPE_NFCC_STANDBY_OFF: {
      uint8_t CMD_NFCC_STANDBY_OFF[] = {0x2F, 0x00, 0x01, 0x00};
      cmdLen = sizeof(CMD_NFCC_STANDBY_OFF);
      memcpy(aCmdBuf, CMD_NFCC_STANDBY_OFF, cmdLen);
      break;
    }
    case CMD_TYPE_NFCC_DISC_MAP: {
      uint8_t CMD_NFCC_DISC_MAP[] = {0x21, 0x00, 0x04, 0x01, 0x04, 0x01, 0x02};
      cmdLen = sizeof(CMD_NFCC_DISC_MAP);
      memcpy(aCmdBuf, CMD_NFCC_DISC_MAP, cmdLen);
      break;
    }
    case CMD_TYPE_NFCC_DEACTIVATE: {
      uint8_t CMD_NFCC_DEACTIVATE[] = {0x21, 0x06, 0x01, 0x00};
      cmdLen = sizeof(CMD_NFCC_DEACTIVATE);
      memcpy(aCmdBuf, CMD_NFCC_DEACTIVATE, cmdLen);
      break;
    }
    case CMD_TYPE_RF_ON: {
      uint8_t CMD_RF_ON[] = {0x2F, 0x3F, 0x03, 0x32, 0x01, 0x00};
      cmdLen = sizeof(CMD_RF_ON);
      memcpy(aCmdBuf, CMD_RF_ON, cmdLen);
      break;
    }
    case CMD_TYPE_RF_OFF: {
      uint8_t CMD_RF_OFF[] = {0x2F, 0x3F, 0x03, 0x32, 0x00, 0x00};
      cmdLen = sizeof(CMD_RF_OFF);
      memcpy(aCmdBuf, CMD_RF_OFF, cmdLen);
      break;
    }
    case CMD_TYPE_CORE_GET_CONFIG_RFTXCFG0: {
      uint8_t CMD_CORE_GET_CONFIG_RFTXCFG0[] = {0x2F, 0x14, 0x02, 0x60, 0x4E};
      gselfTestData.prestorerftxcfg = rf_tx_cfg_restore[0];
      cmdLen = sizeof(CMD_CORE_GET_CONFIG_RFTXCFG0);
      memcpy(aCmdBuf, CMD_CORE_GET_CONFIG_RFTXCFG0, cmdLen);
      break;
    }
    case CMD_TYPE_CORE_GET_CONFIG_RFTXCFG1: {
      uint8_t CMD_CORE_GET_CONFIG_RFTXCFG1[] = {0x2F, 0x14, 0x02, 0x60, 0x50};
      ;
      gselfTestData.prestorerftxcfg = rf_tx_cfg_restore[1];
      cmdLen = sizeof(CMD_CORE_GET_CONFIG_RFTXCFG1);
      memcpy(aCmdBuf, CMD_CORE_GET_CONFIG_RFTXCFG1, cmdLen);
      break;
    }
    case CMD_TYPE_CORE_GET_CONFIG_RFTXCFG2: {
      uint8_t CMD_CORE_GET_CONFIG_RFTXCFG2[] = {0x2F, 0x14, 0x02, 0x60, 0x4F};
      gselfTestData.prestorerftxcfg = rf_tx_cfg_restore[2];
      cmdLen = sizeof(CMD_CORE_GET_CONFIG_RFTXCFG2);
      memcpy(aCmdBuf, CMD_CORE_GET_CONFIG_RFTXCFG2, cmdLen);
      break;
    }
    case CMD_TYPE_CORE_GET_CONFIG_RFTXCFG3: {
      uint8_t CMD_CORE_GET_CONFIG_RFTXCFG3[] = {0x2F, 0x14, 0x02, 0x12, 0x4E};
      ;
      gselfTestData.prestorerftxcfg = rf_tx_cfg_restore[3];
      cmdLen = sizeof(CMD_CORE_GET_CONFIG_RFTXCFG3);
      memcpy(aCmdBuf, CMD_CORE_GET_CONFIG_RFTXCFG3, cmdLen);
      break;
    }
    case CMD_TYPE_CORE_GET_CONFIG_RFTXCFG4: {
      uint8_t CMD_CORE_GET_CONFIG_RFTXCFG4[] = {0x2F, 0x14, 0x02, 0x12, 0x4F};
      gselfTestData.prestorerftxcfg = rf_tx_cfg_restore[4];
      cmdLen = sizeof(CMD_CORE_GET_CONFIG_RFTXCFG4);
      memcpy(aCmdBuf, CMD_CORE_GET_CONFIG_RFTXCFG4, cmdLen);
      break;
    }
    case CMD_TYPE_CORE_GET_CONFIG_RFTXCFG5: {
      uint8_t CMD_CORE_GET_CONFIG_RFTXCFG5[] = {0x2F, 0x14, 0x02, 0x12, 0x50};
      gselfTestData.prestorerftxcfg = rf_tx_cfg_restore[5];
      cmdLen = sizeof(CMD_CORE_GET_CONFIG_RFTXCFG5);
      memcpy(aCmdBuf, CMD_CORE_GET_CONFIG_RFTXCFG5, cmdLen);
      break;
    }
    case CMD_TYPE_CORE_SET_CONFIG_RFTXCFG: {
      uint8_t CMD_CORE_SET_CONFIG_RFTXCFG[] = {
          0x20, 0x02, 0x37, 0x06, 0xA0, 0x0D, 0x06, 0x60, 0x4E, 0x00,
          0x00, 0x00, 0x00, 0xA0, 0x0D, 0x06, 0x60, 0x50, 0x00, 0x00,
          0x00, 0x00, 0xA0, 0x0D, 0x06, 0x60, 0x4F, 0x00, 0x00, 0x00,
          0x00, 0xA0, 0x0D, 0x06, 0x12, 0x4E, 0x00, 0x00, 0x00, 0x00,
          0xA0, 0x0D, 0x06, 0x12, 0x4F, 0x00, 0x00, 0x00, 0x00, 0xA0,
          0x0D, 0x06, 0x12, 0x50, 0x00, 0x00, 0x00, 0x00};

      if (gselfTestData.isStored && !gselfTestData.fSetResFreq) {
        /* Copy stored RF_TRANSITION_CFG */
        gselfTestData.isStored = false;
        if (gselfTestData.prestorerftxcfg == NULL)
          return cmdLen;  // Failure case
        uint8_t* temp = CMD_CORE_SET_CONFIG_RFTXCFG;
        for (int i = 0x00; i < RF_TXCFG_MAX_NUM_FLAGS; i++) {
          temp += MAX_RF_TX_CFG_OFFSET_VALUE;
          memcpy((void*)(temp), (void*)rf_tx_cfg_restore[i],
                  sizeof(rf_tx_cfg_restore[i]));
        }
        gselfTestData.prestorerftxcfg = NULL;
        memset(rf_tx_cfg_restore, 0x00, sizeof(rf_tx_cfg_restore));
      }
      cmdLen = sizeof(CMD_CORE_SET_CONFIG_RFTXCFG);
      memcpy(aCmdBuf, CMD_CORE_SET_CONFIG_RFTXCFG, cmdLen);
      break;
    }
    case CMD_TYPE_NXP_PROP_FLASH_TO_ROM: {
      uint8_t CMD_CORE_GET_CONFIG_RFTXCFG5[] = {0x2F, 0x21, 0x00};
      cmdLen = sizeof(CMD_CORE_GET_CONFIG_RFTXCFG5);
      memcpy(aCmdBuf, CMD_CORE_GET_CONFIG_RFTXCFG5, cmdLen);
      break;
    }
    case CMD_TYPE_PRBS_ON: {
      uint8_t CMD_EXTN_PRBS_START[] = {0x2F, 0x30, 0x06, 0x00, 0x00,
                                       0xFF, 0xFF, 0x01, 0xFF};
      CMD_EXTN_PRBS_START[5] = gselfTestData.prbsTech;  // TECH
      CMD_EXTN_PRBS_START[6] = gselfTestData.prbsRate;  // BITRATE
      cmdLen = sizeof(CMD_EXTN_PRBS_START);
      memcpy(aCmdBuf, CMD_EXTN_PRBS_START, cmdLen);
      break;
    }
    case CMD_TYPE_SPC_NTF_EN: {
      uint8_t CMD_SPC_NTF_EN[] = {0x2F, 0x3D, 0x0F, 0x30, 0x00, 0xE0, 0x2E, 0x32,
              0x51, 0x27, 0x05, 0xED, 0x91, 0x10, 0x01, 0x00, 0x12, 0x1F};
      cmdLen = sizeof(CMD_SPC_NTF_EN);
      memcpy(aCmdBuf, CMD_SPC_NTF_EN, cmdLen);
      break;
    }
    case CMD_TYPE_SPC_NTF_EN_SN220: {
      uint8_t CMD_SPC_NTF_EN[] = {0x2F, 0x3D, 0x22, 0x30, 0x00, 0x28, 0x2F, 0x00,
                0x03, 0x51, 0x00, 0x00, 0x00, 0x00, 0x01, 0x03, 0x00, 0x0A, 0x14,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
      std::vector<uint8_t> spc_cfg;
      if (NfcConfig::hasKey(NAME_NXP_SPC_CONF_BLK))
        spc_cfg = NfcConfig::getBytes(NAME_NXP_SPC_CONF_BLK);
      if (spc_cfg.size() == 4) {
        LOG(INFO)
            << StringPrintf("NXP_SPC_CFG, SN220, 0x%02X%02X%02X%02X", spc_cfg[0], spc_cfg[1], spc_cfg[2], spc_cfg[3]);
        CMD_SPC_NTF_EN[10] = spc_cfg[0];
        CMD_SPC_NTF_EN[11] = spc_cfg[1];
        CMD_SPC_NTF_EN[12] = spc_cfg[2];
        CMD_SPC_NTF_EN[13] = spc_cfg[3];
      }
      else if (spc_cfg.size() == 37) {
        memcpy(CMD_SPC_NTF_EN, spc_cfg.data(), spc_cfg.size());
      }
      cmdLen = sizeof(CMD_SPC_NTF_EN);
      memcpy(aCmdBuf, CMD_SPC_NTF_EN, cmdLen);
      break;
    }
    case CMD_TYPE_SPC_19_2MHZ_BLK1: {
      uint8_t CMD_SPC_BLK1[] = {0x2F, 0x3D, 0x7E, 0x30, 0x01, 0x80, 0x0C, 0x00,
              0x66, 0x41, 0x19, 0x03, 0x66, 0xC0, 0x0C, 0x03, 0x66, 0x81, 0x99,
              0x03, 0x66, 0x81, 0x99, 0x01, 0x66, 0xC1, 0x99, 0x04, 0x66, 0xC1,
              0x99, 0x02, 0x66, 0xC1, 0x99, 0x00, 0x66, 0x01, 0x9A, 0x03, 0x66,
              0x00, 0x8D, 0x00, 0x66, 0x40, 0x8D, 0x04, 0x66, 0x40, 0x8D, 0x03,
              0x66, 0x40, 0x8D, 0x02, 0x66, 0x40, 0x8D, 0x01, 0x66, 0x40, 0x8D,
              0x00, 0x66, 0xC1, 0x9A, 0x03, 0x66, 0xC1, 0x9A, 0x01, 0x66, 0x01,
              0x9B, 0x04, 0x66, 0x01, 0x9B, 0x02, 0x66, 0x01, 0x9B, 0x00, 0x66,
              0x41, 0x9B, 0x03, 0x66, 0xC0, 0x0D, 0x03, 0x66, 0xC0, 0x0D, 0x02,
              0x66, 0xC0, 0x0D, 0x01, 0x66, 0xC0, 0x0D, 0x00, 0x66, 0xC1, 0x1B,
              0x03, 0x66, 0x00, 0x0E, 0x03, 0x66, 0x00, 0x0E, 0x02, 0x66, 0x01,
              0x9C, 0x01, 0x66, 0x41, 0x9C, 0x04, 0x66, 0x41, 0x9C, 0x02, 0x66};
      cmdLen = sizeof(CMD_SPC_BLK1);
      memcpy(aCmdBuf, CMD_SPC_BLK1, cmdLen);
      break;
    }
    case CMD_TYPE_SPC_19_2MHZ_BLK1_SN220: {
      uint8_t CMD_SPC_BLK1[] = {0x2F, 0x3D, 0x7E, 0x30, 0x01, 0xC0, 0x8C, 0x03,
              0x66, 0xC0, 0x8C, 0x02, 0x66, 0xC0, 0x8C, 0x01, 0x66, 0xC0, 0x8C,
              0x00, 0x66, 0x00, 0x8D, 0x04, 0x66, 0x00, 0x8D, 0x03, 0x66, 0x00,
              0x8D, 0x02, 0x66, 0x00, 0x8D, 0x01, 0x66, 0x00, 0x8D, 0x00, 0x66,
              0x40, 0x8D, 0x04, 0x66, 0x40, 0x8D, 0x03, 0x66, 0x40, 0x8D, 0x02,
              0x66, 0x40, 0x8D, 0x01, 0x66, 0x40, 0x8D, 0x00, 0x66, 0x80, 0x8D,
              0x04, 0x66, 0x80, 0x8D, 0x03, 0x66, 0x80, 0x8D, 0x02, 0x66, 0x80,
              0x8D, 0x01, 0x66, 0x80, 0x8D, 0x00, 0x66, 0xC0, 0x8D, 0x04, 0x66,
              0xC0, 0x8D, 0x03, 0x66, 0xC0, 0x8D, 0x02, 0x66, 0xC0, 0x8D, 0x01,
              0x66, 0xC0, 0x8D, 0x00, 0x66, 0x00, 0x8E, 0x04, 0x66, 0x00, 0x8E,
              0x03, 0x66, 0x00, 0x8E, 0x02, 0x66, 0x00, 0x8E, 0x01, 0x66, 0x00,
              0x8E, 0x00, 0x66, 0x40, 0x8E, 0x04, 0x66, 0x40, 0x8E, 0x03, 0x66};
      cmdLen = sizeof(CMD_SPC_BLK1);
      memcpy(aCmdBuf, CMD_SPC_BLK1, cmdLen);
      break;
    }
    case CMD_TYPE_SPC_19_2MHZ_BLK2: {
      uint8_t CMD_SPC_BLK2[] = {0x2F, 0x3D, 0x7E, 0x30, 0x02, 0x41, 0x9C, 0x00,
              0x66, 0x81, 0x9C, 0x03, 0x66, 0x40, 0x8E, 0x00, 0x66, 0x80, 0x8E,
              0x04, 0x66, 0x80, 0x8E, 0x03, 0x66, 0x80, 0x8E, 0x02, 0x66, 0x80,
              0x8E, 0x01, 0x66, 0x80, 0x8E, 0x00, 0x66, 0xC0, 0x8E, 0x04, 0x66,
              0x41, 0x9D, 0x01, 0x66, 0x81, 0x9D, 0x04, 0x66, 0x81, 0x9D, 0x02,
              0x66, 0x81, 0x9D, 0x00, 0x66, 0xC1, 0x9D, 0x03, 0x66, 0x00, 0x0F,
              0x03, 0x66, 0x00, 0x0F, 0x02, 0x66, 0x00, 0x0F, 0x01, 0x66, 0x00,
              0x0F, 0x00, 0x66, 0x41, 0x1E, 0x03, 0x66, 0x40, 0x0F, 0x03, 0x66,
              0x40, 0x0F, 0x02, 0x66, 0x81, 0x9E, 0x01, 0x66, 0xC1, 0x9E, 0x04,
              0x66, 0xC1, 0x9E, 0x02, 0x66, 0xC1, 0x9E, 0x00, 0x66, 0x01, 0x9F,
              0x03, 0x66, 0x80, 0x8F, 0x00, 0x66, 0xC0, 0x8F, 0x04, 0x66, 0xC0,
              0x8F, 0x03, 0x66, 0xC0, 0x8F, 0x02, 0x66, 0xC0, 0x8F, 0x01, 0x66};
      cmdLen = sizeof(CMD_SPC_BLK2);
      memcpy(aCmdBuf, CMD_SPC_BLK2, cmdLen);
      break;
    }
    case CMD_TYPE_SPC_19_2MHZ_BLK2_SN220: {
      uint8_t CMD_SPC_BLK2[] = {0x2F, 0x3D, 0x7E, 0x30, 0x02, 0x40, 0x8E, 0x02,
              0x66, 0x40, 0x8E, 0x01, 0x66, 0x40, 0x8E, 0x00, 0x66, 0x80, 0x8E,
              0x04, 0x66, 0x80, 0x8E, 0x03, 0x66, 0x80, 0x8E, 0x02, 0x66, 0x80,
              0x8E, 0x01, 0x66, 0x80, 0x8E, 0x00, 0x66, 0xC0, 0x8E, 0x04, 0x66,
              0xC0, 0x8E, 0x03, 0x66, 0xC0, 0x8E, 0x02, 0x66, 0xC0, 0x8E, 0x01,
              0x66, 0xC0, 0x8E, 0x00, 0x66, 0x00, 0x8F, 0x04, 0x66, 0x00, 0x8F,
              0x03, 0x66, 0x00, 0x8F, 0x02, 0x66, 0x00, 0x8F, 0x01, 0x66, 0x00,
              0x8F, 0x00, 0x66, 0x40, 0x8F, 0x04, 0x66, 0x40, 0x8F, 0x03, 0x66,
              0x40, 0x8F, 0x02, 0x66, 0x40, 0x8F, 0x01, 0x66, 0x40, 0x8F, 0x00,
              0x66, 0x80, 0x8F, 0x04, 0x66, 0x80, 0x8F, 0x03, 0x66, 0x80, 0x8F,
              0x02, 0x66, 0x80, 0x8F, 0x01, 0x66, 0x80, 0x8F, 0x00, 0x66, 0xC0,
              0x8F, 0x04, 0x66, 0xC0, 0x8F, 0x03, 0x66, 0xC0, 0x8F, 0x02, 0x66};
      cmdLen = sizeof(CMD_SPC_BLK2);
      memcpy(aCmdBuf, CMD_SPC_BLK2, cmdLen);
      break;
    }
    case CMD_TYPE_SPC_19_2MHZ_BLK3: {
      uint8_t CMD_SPC_BLK3[] = {0x2F, 0x3D, 0x4E, 0x30, 0x03, 0xC0, 0x8F, 0x00,
              0x66, 0x00, 0x90, 0x04, 0x66, 0xC1, 0x9F, 0x01, 0x66, 0x01, 0xA0,
              0x04, 0x66, 0x01, 0xA0, 0x02, 0x66, 0x01, 0xA0, 0x00, 0x66, 0x41,
              0xA0, 0x03, 0x66, 0x40, 0x10, 0x03, 0x66, 0x40, 0x10, 0x02, 0x66,
              0x40, 0x10, 0x01, 0x66, 0x40, 0x10, 0x00, 0x66, 0xC1, 0x20, 0x03,
              0x66, 0x80, 0x10, 0x03, 0x66, 0x01, 0xA1, 0x03, 0x66, 0x01, 0xA1,
              0x01, 0x66, 0x41, 0xA1, 0x04, 0x66, 0x41, 0xA1, 0x02, 0x66, 0x41,
              0xA1, 0x00, 0x66, 0x81, 0xA1, 0x03, 0x66};
      cmdLen = sizeof(CMD_SPC_BLK3);
      memcpy(aCmdBuf, CMD_SPC_BLK3, cmdLen);
      break;
    }
    case CMD_TYPE_SPC_19_2MHZ_BLK3_SN220: {
      uint8_t CMD_SPC_BLK3[] = {0x2F, 0x3D, 0x4E, 0x30, 0x03, 0xC0, 0x8F, 0x01,
              0x66, 0xC0, 0x8F, 0x00, 0x66, 0x00, 0x90, 0x04, 0x66, 0x00, 0x90,
              0x03, 0x66, 0x00, 0x90, 0x02, 0x66, 0x00, 0x90, 0x01, 0x66, 0x00,
              0x90, 0x00, 0x66, 0x40, 0x90, 0x04, 0x66, 0x40, 0x90, 0x03, 0x66,
              0x40, 0x90, 0x02, 0x66, 0x40, 0x90, 0x01, 0x66, 0x40, 0x90, 0x00,
              0x66, 0x80, 0x90, 0x04, 0x66, 0x80, 0x90, 0x03, 0x66, 0x80, 0x90,
              0x02, 0x66, 0x80, 0x90, 0x01, 0x66, 0x80, 0x90, 0x00, 0x66, 0xC0,
              0x90, 0x04, 0x66, 0xC0, 0x90, 0x03, 0x66};
      cmdLen = sizeof(CMD_SPC_BLK3);
      memcpy(aCmdBuf, CMD_SPC_BLK3, cmdLen);
      break;
    }
    case CMD_TYPE_SPC_26MHZ_BLK1_SN220: {
      uint8_t CMD_SPC_BLK1[] = {0x2F, 0x3D, 0x7E, 0x30, 0x01, 0x42, 0xA5, 0x01,
              0x62, 0x82, 0xA5, 0x03, 0x62, 0x82, 0xA5, 0x00, 0x62, 0xC2, 0xA5,
              0x02, 0x62, 0x02, 0xA6, 0x04, 0x62, 0x02, 0xA6, 0x01, 0x62, 0x42,
              0xA6, 0x03, 0x62, 0x42, 0xA6, 0x00, 0x62, 0x82, 0xA6, 0x02, 0x62,
              0xC2, 0xA6, 0x04, 0x62, 0xC2, 0xA6, 0x01, 0x62, 0x02, 0xA7, 0x03,
              0x62, 0x02, 0xA7, 0x00, 0x62, 0x42, 0xA7, 0x02, 0x62, 0x82, 0xA7,
              0x04, 0x62, 0x82, 0xA7, 0x01, 0x62, 0xC2, 0xA7, 0x03, 0x62, 0xC2,
              0xA7, 0x00, 0x62, 0x02, 0xA8, 0x02, 0x62, 0x42, 0xA8, 0x04, 0x62,
              0x42, 0xA8, 0x01, 0x62, 0x82, 0xA8, 0x03, 0x62, 0x82, 0xA8, 0x00,
              0x62, 0xC2, 0xA8, 0x02, 0x62, 0x02, 0xA9, 0x04, 0x62, 0x02, 0xA9,
              0x01, 0x62, 0x42, 0xA9, 0x03, 0x62, 0x42, 0xA9, 0x00, 0x62, 0x82,
              0xA9, 0x02, 0x62, 0xC2, 0xA9, 0x04, 0x62, 0xC2, 0xA9, 0x01, 0x62};
      cmdLen = sizeof(CMD_SPC_BLK1);
      memcpy(aCmdBuf, CMD_SPC_BLK1, cmdLen);
      break;
    }
    case CMD_TYPE_SPC_26MHZ_BLK2_SN220: {
      uint8_t CMD_SPC_BLK2[] = {0x2F, 0x3D, 0x7E, 0x30, 0x02, 0x02, 0xAA, 0x03,
              0x62, 0x02, 0xAA, 0x00, 0x62, 0x42, 0xAA, 0x02, 0x62, 0x82, 0xAA,
              0x04, 0x62, 0x82, 0xAA, 0x01, 0x62, 0xC2, 0xAA, 0x03, 0x62, 0xC2,
              0xAA, 0x00, 0x62, 0x02, 0xAB, 0x02, 0x62, 0x42, 0xAB, 0x04, 0x62,
              0x42, 0xAB, 0x01, 0x62, 0x82, 0xAB, 0x03, 0x62, 0x82, 0xAB, 0x00,
              0x62, 0xC2, 0xAB, 0x02, 0x62, 0x02, 0xAC, 0x04, 0x62, 0x02, 0xAC,
              0x01, 0x62, 0x42, 0xAC, 0x03, 0x62, 0x42, 0xAC, 0x00, 0x62, 0x82,
              0xAC, 0x02, 0x62, 0xC2, 0xAC, 0x04, 0x62, 0xC2, 0xAC, 0x01, 0x62,
              0x02, 0xAD, 0x03, 0x62, 0x02, 0xAD, 0x00, 0x62, 0x42, 0xAD, 0x02,
              0x62, 0x82, 0xAD, 0x04, 0x62, 0x82, 0xAD, 0x01, 0x62, 0xC2, 0xAD,
              0x03, 0x62, 0xC2, 0xAD, 0x00, 0x62, 0x02, 0xAE, 0x02, 0x62, 0x42,
              0xAE, 0x04, 0x62, 0x42, 0xAE, 0x01, 0x62, 0x82, 0xAE, 0x03, 0x62};
      cmdLen = sizeof(CMD_SPC_BLK2);
      memcpy(aCmdBuf, CMD_SPC_BLK2, cmdLen);
      break;
    }
    case CMD_TYPE_SPC_26MHZ_BLK3_SN220: {
      uint8_t CMD_SPC_BLK3[] = {0x2F, 0x3D, 0x4E, 0x30, 0x03, 0x82, 0xAE, 0x00,
              0x62, 0xC2, 0xAE, 0x02, 0x62, 0x02, 0xAF, 0x04, 0x62, 0x02, 0xAF,
              0x01, 0x62, 0x42, 0xAF, 0x03, 0x62, 0x42, 0xAF, 0x00, 0x62, 0x82,
              0xAF, 0x02, 0x62, 0xC2, 0xAF, 0x04, 0x62, 0xC2, 0xAF, 0x01, 0x62,
              0x02, 0xB0, 0x03, 0x62, 0x02, 0xB0, 0x00, 0x62, 0x42, 0xB0, 0x02,
              0x62, 0x82, 0xB0, 0x04, 0x62, 0x82, 0xB0, 0x01, 0x62, 0xC2, 0xB0,
              0x03, 0x62, 0xC2, 0xB0, 0x00, 0x62, 0x02, 0xB1, 0x02, 0x62, 0x42,
              0xB1, 0x04, 0x62, 0x42, 0xB1, 0x01, 0x62};
      cmdLen = sizeof(CMD_SPC_BLK3);
      memcpy(aCmdBuf, CMD_SPC_BLK3, cmdLen);
      break;
    }
    case CMD_TYPE_SPC_38_4MHZ_BLK1_SN220: {
      uint8_t CMD_SPC_BLK1[] = {0x2F, 0x3D, 0x7E, 0x30, 0x01, 0x81, 0x8C, 0x00,
              0x66, 0xC1, 0x8C, 0x04, 0x66, 0xC1, 0x8C, 0x03, 0x66, 0xC1, 0x8C,
              0x02, 0x66, 0xC1, 0x8C, 0x01, 0x66, 0xC1, 0x8C, 0x00, 0x66, 0x01,
              0x8D, 0x04, 0x66, 0x01, 0x8D, 0x03, 0x66, 0x01, 0x8D, 0x02, 0x66,
              0x01, 0x8D, 0x01, 0x66, 0x01, 0x8D, 0x00, 0x66, 0x41, 0x8D, 0x04,
              0x66, 0x41, 0x8D, 0x03, 0x66, 0x41, 0x8D, 0x02, 0x66, 0x41, 0x8D,
              0x01, 0x66, 0x41, 0x8D, 0x00, 0x66, 0x81, 0x8D, 0x04, 0x66, 0x81,
              0x8D, 0x03, 0x66, 0x81, 0x8D, 0x02, 0x66, 0x81, 0x8D, 0x01, 0x66,
              0x81, 0x8D, 0x00, 0x66, 0xC1, 0x8D, 0x04, 0x66, 0xC1, 0x8D, 0x03,
              0x66, 0xC1, 0x8D, 0x02, 0x66, 0xC1, 0x8D, 0x01, 0x66, 0xC1, 0x8D,
              0x00, 0x66, 0x01, 0x8E, 0x04, 0x66, 0x01, 0x8E, 0x03, 0x66, 0x01,
              0x8E, 0x02, 0x66, 0x01, 0x8E, 0x01, 0x66, 0x01, 0x8E, 0x00, 0x66};
      cmdLen = sizeof(CMD_SPC_BLK1);
      memcpy(aCmdBuf, CMD_SPC_BLK1, cmdLen);
      break;
    }
    case CMD_TYPE_SPC_38_4MHZ_BLK2_SN220: {
      uint8_t CMD_SPC_BLK2[] = {0x2F, 0x3D, 0x7E, 0x30, 0x02, 0x41, 0x8E, 0x04,
              0x66, 0x41, 0x8E, 0x03, 0x66, 0x41, 0x8E, 0x02, 0x66, 0x41, 0x8E,
              0x01, 0x66, 0x41, 0x8E, 0x00, 0x66, 0x81, 0x8E, 0x04, 0x66, 0x81,
              0x8E, 0x03, 0x66, 0x81, 0x8E, 0x02, 0x66, 0x81, 0x8E, 0x01, 0x66,
              0x81, 0x8E, 0x00, 0x66, 0xC1, 0x8E, 0x04, 0x66, 0xC1, 0x8E, 0x03,
              0x66, 0xC1, 0x8E, 0x02, 0x66, 0xC1, 0x8E, 0x01, 0x66, 0xC1, 0x8E,
              0x00, 0x66, 0x01, 0x8F, 0x04, 0x66, 0x01, 0x8F, 0x03, 0x66, 0x01,
              0x8F, 0x02, 0x66, 0x01, 0x8F, 0x01, 0x66, 0x01, 0x8F, 0x00, 0x66,
              0x41, 0x8F, 0x04, 0x66, 0x41, 0x8F, 0x03, 0x66, 0x41, 0x8F, 0x02,
              0x66, 0x41, 0x8F, 0x01, 0x66, 0x41, 0x8F, 0x00, 0x66, 0x81, 0x8F,
              0x04, 0x66, 0x81, 0x8F, 0x03, 0x66, 0x81, 0x8F, 0x02, 0x66, 0x81,
              0x8F, 0x01, 0x66, 0x81, 0x8F, 0x00, 0x66, 0xC1, 0x8F, 0x04, 0x66};
      cmdLen = sizeof(CMD_SPC_BLK2);
      memcpy(aCmdBuf, CMD_SPC_BLK2, cmdLen);
      break;
    }
    case CMD_TYPE_SPC_38_4MHZ_BLK3_SN220: {
      uint8_t CMD_SPC_BLK3[] = {0x2F, 0x3D, 0x4E, 0x30, 0x03, 0xC1, 0x8F, 0x03,
              0x66, 0xC1, 0x8F, 0x02, 0x66, 0xC1, 0x8F, 0x01, 0x66, 0xC1, 0x8F,
              0x00, 0x66, 0x01, 0x90, 0x04, 0x66, 0x01, 0x90, 0x03, 0x66, 0x01,
              0x90, 0x02, 0x66, 0x01, 0x90, 0x01, 0x66, 0x01, 0x90, 0x00, 0x66,
              0x41, 0x90, 0x04, 0x66, 0x41, 0x90, 0x03, 0x66, 0x41, 0x90, 0x02,
              0x66, 0x41, 0x90, 0x01, 0x66, 0x41, 0x90, 0x00, 0x66, 0x81, 0x90,
              0x04, 0x66, 0x81, 0x90, 0x03, 0x66, 0x81, 0x90, 0x02, 0x66, 0x81,
              0x90, 0x01, 0x66, 0x81, 0x90, 0x00, 0x66};
      cmdLen = sizeof(CMD_SPC_BLK3);
      memcpy(aCmdBuf, CMD_SPC_BLK3, cmdLen);
      break;
    }
    case CMD_TYPE_SPC_START: {
      uint8_t CMD_SPC_START[] = {0x2F, 0x3D, 0x01, 0x31};
      cmdLen = sizeof(CMD_SPC_START);
      memcpy(aCmdBuf, CMD_SPC_START, cmdLen);
      break;
    }
    case CMD_TYPE_SPC_ROUTE: {
      uint8_t CMD_SPC_ROUTE[] = {0x21, 0x01, 0x1C, 0x00, 0x05, 0x03, 0x04, 0xC0,
              0x39, 0xFE, 0xFF, 0x01, 0x03, 0x00, 0x11, 0x04, 0x00, 0x03, 0xC0,
              0x3F, 0x00, 0x00, 0x03, 0xC0, 0x3F, 0x01, 0x00, 0x03, 0xC0, 0x3F, 0x02};
      cmdLen = sizeof(CMD_SPC_ROUTE);
      memcpy(aCmdBuf, CMD_SPC_ROUTE, cmdLen);
      break;
    }
    case CMD_TYPE_NFCC_ALLOW_CHANGE_PARAM: {
      uint8_t CMD_SPC_ROUTE[] = {0x20, 0x02, 0x04, 0x01, 0x85, 0x01, 0x01};
      cmdLen = sizeof(CMD_SPC_ROUTE);
      memcpy(aCmdBuf, CMD_SPC_ROUTE, cmdLen);
      break;
    }
    default:
      LOG(INFO) << StringPrintf("Command not supported");
      break;
  }
  return cmdLen;
}

/*******************************************************************************
 ** Sends NFCC cmd to enable/select default eUICC port
 ** @param type - defines the port to be selected
 ** @return status of NFCC write cmd
 *******************************************************************************/
static int SeteUICCdefalutPort(uint8_t type) {
  tNFA_STATUS status = NFA_STATUS_REJECTED;
  uint8_t cmd_setport[] = {0x20, 0x02, 0x05, 0x01,
                           0xA1, 0x97, 0x01, 0x00 /* port num */};

  if (type == TEST_TYPE_SELECT_EUICC_PORT_1) {
    cmd_setport[7] = 0x00;
  } else if (type == TEST_TYPE_SELECT_EUICC_PORT_2) {
    cmd_setport[7] = 0x01;
  } else {
    return NFA_STATUS_REJECTED;
  }
  if (isDiscoveryStarted()) startRfDiscovery(false);

  status = NxpNfc_Write_Cmd_Common(sizeof(cmd_setport), cmd_setport);

  if (!isDiscoveryStarted()) startRfDiscovery(true);
  return status;
}

/*******************************************************************************
 ** Executes NFC self-test requests from service.
 ** @param  aType denotes type of self-test
 ** @return status SUCCESS or FAILED.
 *******************************************************************************/
tNFA_STATUS NfcSelfTest::doNfccSelfTest(int aType) {
  tNFA_STATUS status = NFA_STATUS_FAILED;
  LOG(INFO) << StringPrintf("Self-Test Type %d", aType);
  SelfTestType = aType;
  uint8_t clk_freq = 4; //4 for 26MHz and 5 for 38.4 MHz.

  switch (aType) {
    case TEST_TYPE_RESTORE_RFTXCFG:
      status = PerformResonantFreq(false);
      break;
    case TEST_TYPE_SET_RFTXCFG_RESONANT_FREQ:
      status = PerformResonantFreq(true);
      break;
    case TEST_TYPE_RF_ON:
      status = PerformRFTest(true);
      break;
    case TEST_TYPE_RF_OFF:
      status = PerformRFTest(false);
      break;
    case TEST_TYPE_TRANSAC_A:
      status = PerformTransacAB(TEST_TYPE_TRANSAC_A);
      break;
    case TEST_TYPE_TRANSAC_B:
      status = PerformTransacAB(TEST_TYPE_TRANSAC_B);
      break;
    case TEST_TYPE_PRBS_ON:
      status = PerformPrbs(true);
      break;
    case TEST_TYPE_PRBS_OFF:
      status = PerformPrbs(false);
      break;
    case TEST_TYPE_SPC:
      status = PerformSPCTest(clk_freq);
      break;
    case TEST_TYPE_SELECT_EUICC_PORT_1:
    case TEST_TYPE_SELECT_EUICC_PORT_2:
      status = SeteUICCdefalutPort(aType);
      break;
    default:
      LOG(ERROR)
          << StringPrintf("Self-test type invalid/not supported");
      SelfTestType = TEST_TYPE_NONE;
      break;
  }
  SelfTestType = TEST_TYPE_NONE;
  return status;
}

tNFA_STATUS NfcSelfTest::PerformRFTest(bool on) {
  tNFA_STATUS status = NFA_STATUS_FAILED;
  uint8_t* pp = NULL;
  uint8_t RFTestCmdSeq[5] = {
      CMD_TYPE_CORE_RESET,
      CMD_TYPE_CORE_INIT,
      CMD_TYPE_NXP_PROP_EXT,
  };
  /* Stop RF Discovery */
  if (isDiscoveryStarted()) startRfDiscovery(false);

  pp = RFTestCmdSeq + 2 + 1;
  if (on) {
    *pp++ = CMD_TYPE_NFCC_STANDBY_OFF;
    *pp = CMD_TYPE_RF_ON;
  } else {
    *pp++ = CMD_TYPE_RF_OFF;
    *pp = CMD_TYPE_NFCC_STANDBY_ON;
  }

  status = executeCmdSeq(RFTestCmdSeq, sizeof(RFTestCmdSeq));
  LOG(INFO) << StringPrintf("status=%u", status);
  return status;
}

tNFA_STATUS NfcSelfTest::PerformTransacAB(uint8_t aType) {
  tNFA_STATUS status = NFA_STATUS_FAILED;
  uint8_t tech_mask = 0;
  uint8_t readerProfileSelCfg[] = {0x20, 0x02, 0x09, 0x02, 0xA0, 0x3F,
                                   0x01, 0x01, 0xA0, 0x44, 0x01, 0x63};
  uint8_t val85[] = {0x01};
  uint8_t NFCInitCmdSeq[3] = {CMD_TYPE_CORE_RESET, CMD_TYPE_CORE_INIT,
                              CMD_TYPE_NXP_PROP_EXT};

  /* Stop RF Discovery */
  if (isDiscoveryStarted()) startRfDiscovery(false);

  {
    SyncEventGuard gaurd(sChangeDiscTechEvent);
    status = NFA_ChangeDiscoveryTech(0x00, 0x00);
    if (status == NFA_STATUS_OK) sChangeDiscTechEvent.wait(2 * ONE_SECOND_MS);
  }

  if (aType == TEST_TYPE_TRANSAC_A) {
    tech_mask = NFA_TECHNOLOGY_MASK_A;
  } else {
    readerProfileSelCfg[11] = 0x43;
    tech_mask = NFA_TECHNOLOGY_MASK_A | NFA_TECHNOLOGY_MASK_B;
  }

  status = executeCmdSeq(NFCInitCmdSeq, sizeof(NFCInitCmdSeq));
  if (status == NFA_STATUS_OK) {
    {
      SyncEventGuard guard(gNfaSetConfigEvent);
      status = NFA_SetConfig(0x85, 1, val85);
      if (status == NFA_STATUS_OK) gNfaSetConfigEvent.wait(2 * ONE_SECOND_MS);
    }
    {
      SyncEventGuard guard(gselfTestData.NxpSelfTestEvt);
      status =
          NFA_SendRawVsCommand(sizeof(readerProfileSelCfg), readerProfileSelCfg,
                               NxpResponse_SelfTest_Cb);
      if (status == NFA_STATUS_OK)
        gselfTestData.NxpSelfTestEvt.wait(2 * ONE_SECOND_MS);
    }
  } else {
    LOG(INFO)
        << StringPrintf("failed in to reset and init NFCC");
  }

  if (status == NFA_STATUS_OK) {
    uint8_t discMapCmd[] = {CMD_TYPE_NFCC_DISC_MAP};
    status = executeCmdSeq(discMapCmd, sizeof(discMapCmd));
  }

  if (status == NFA_STATUS_OK) {
    NFA_SetEmvCoState(TRUE);
    SyncEventGuard gaurd(sChangeDiscTechEvent);
    if ((status = NFA_ChangeDiscoveryTech(tech_mask, 0x00)) == NFA_STATUS_OK) {
      sChangeDiscTechEvent.wait(2 * ONE_SECOND_MS);
      startRfDiscovery(true);
      {
        SyncEventGuard gaurd(mSelfTestTransacAB);
        mSelfTestTransacAB.wait(30 * ONE_SECOND_MS);
      }
    }
  } else {
    SyncEventGuard gaurd(sChangeDiscTechEvent);
    status = NFA_ChangeDiscoveryTech(tech_mask, 0x00);
    if (status == NFA_STATUS_OK) sChangeDiscTechEvent.wait(2 * ONE_SECOND_MS);
    startRfDiscovery(true);
  }

  NFA_SetEmvCoState(false);
  startRfDiscovery(false);
  LOG(INFO) << StringPrintf("exiting status=%u", status);
  return status;
}

void NfcSelfTest::ActivatedNtf_Cb() {
  SyncEventGuard gaurd(mSelfTestTransacAB);
  mSelfTestTransacAB.notifyOne();
}

/*******************************************************************************
 ** Executes: 1. Save the current value of the NFCC's RF_TRANSITION_CFG.
 **           2. Sets the RF_TRANSITION_CFG value to generate Resonant
 *Frequency.
 ** @param  None
 ** @return status SUCCESS or FAILED.
 *******************************************************************************/
tNFA_STATUS NfcSelfTest::setResonantFreq() {
  tNFA_STATUS status = NFA_STATUS_FAILED;
  uint8_t resFreqCmdSeq[] = {
      CMD_TYPE_CORE_GET_CONFIG_RFTXCFG0, CMD_TYPE_CORE_GET_CONFIG_RFTXCFG1,
      CMD_TYPE_CORE_GET_CONFIG_RFTXCFG2, CMD_TYPE_CORE_GET_CONFIG_RFTXCFG3,
      CMD_TYPE_CORE_GET_CONFIG_RFTXCFG4, CMD_TYPE_CORE_GET_CONFIG_RFTXCFG5,
      CMD_TYPE_CORE_SET_CONFIG_RFTXCFG,  CMD_TYPE_NXP_PROP_FLASH_TO_ROM};

  gselfTestData.fSetResFreq = true;
  status = executeCmdSeq(resFreqCmdSeq, sizeof(resFreqCmdSeq));
  if (NFA_STATUS_OK == status)
    gselfTestData.isStored =
        true; /* flag should be cleared once RFTXCFGs are restored */
  gselfTestData.fSetResFreq = false;
  return status;
}

/********************************************************************************
 ** Executes Restore the RF_TRANSITION_CFG values, if stored by the
 *setResonantFreq()
 ** @param  None
 ** @return status SUCCESS or FAILED.
 *******************************************************************************/
tNFA_STATUS NfcSelfTest::restoreRfTxCfg() {
  tNFA_STATUS status = NFA_STATUS_FAILED;
  uint8_t rfTestCmdSeq[] = {CMD_TYPE_CORE_SET_CONFIG_RFTXCFG,
                            CMD_TYPE_NXP_PROP_FLASH_TO_ROM};

  status = executeCmdSeq(rfTestCmdSeq, sizeof(rfTestCmdSeq));

  return status;
}

/*******************************************************************************
 ** Executes: Updates the RF_TRANSITION_CFG as per the command type
 ** @param  on denotes
 **         TRUE  - setResonantFreq()
 **         FALSE - restoreRfTxCfg()
 ** @return status SUCCESS or FAILED.
 *******************************************************************************/
tNFA_STATUS NfcSelfTest::PerformResonantFreq(bool on) {
  tNFA_STATUS status;

  if (on)
    status = setResonantFreq();
  else
    status = restoreRfTxCfg();

  LOG(INFO)
      << StringPrintf(" PerformResonantFreq status=%u", status);

  return status;
}

/*******************************************************************************
 ** Callback: Callback to be registered with NFA_RegVSCback()
 ** @param    Event: for which the callback is invoked
 **           param_len: Len of the Parameters passed
 **           p_param: Pointer to the event param
 ** @return   None
 *******************************************************************************/
static void nfaVSCNtfCallback(uint8_t event, uint16_t param_len, uint8_t *p_param) {
  (void)event;
  if ((p_param[0] == 0x6F) && (p_param[1] == 0x3D) &&
      ((p_param[2] == 0x06) || (p_param[2] == 0x07))) {
    /*If status byte is included offset shifts accordingly*/
    uint8_t offset = p_param[2] - 6;

    uint16_t trim_val = (p_param[7 + offset] << 8) + p_param[8 + offset];
    uint16_t spc_rssi = (p_param[6 + offset] << 8) + p_param[5 + offset];
    LOG(INFO) << StringPrintf("%s MIN_RSSI[%X] at Customer phase"
            "trim value[%X]. min RSSI start index is %X & end index is %X", __func__,
            spc_rssi, trim_val, p_param[3], p_param[4]);
    SyncEventGuard guard (sNfaVscNtfEvent);
    sNfaVscNtfEvent.notifyOne();
  }
}

/*******************************************************************************
 ** Executes: Configures the FW and starts the SPC algorithm to save the customer
 **           phase offset into RF_CUST_PHASE_COMPENSATION.
 ** @param    clk_freq - clock frequency to be used for SPC test
 ** @return status SUCCESS or FAILED.
 *******************************************************************************/
tNFA_STATUS NfcSelfTest::PerformSPCTest(uint8_t clk_freq) {
  tNFA_STATUS status = NFA_STATUS_FAILED;
  uint8_t SPC26MHzTestCmdSeq[] = {CMD_TYPE_CORE_RESET, CMD_TYPE_CORE_INIT,
          CMD_TYPE_NFCC_ALLOW_CHANGE_PARAM, CMD_TYPE_NXP_PROP_EXT, CMD_TYPE_SPC_NTF_EN,
          CMD_TYPE_SPC_26MHZ_BLK1, CMD_TYPE_SPC_26MHZ_BLK2, CMD_TYPE_SPC_26MHZ_BLK3, CMD_TYPE_SPC_START};
  uint8_t SPCTestCmdSeq[] = {CMD_TYPE_CORE_RESET, CMD_TYPE_CORE_INIT,
          CMD_TYPE_NFCC_ALLOW_CHANGE_PARAM, CMD_TYPE_NXP_PROP_EXT, CMD_TYPE_SPC_NTF_EN,
          CMD_TYPE_SPC_19_2MHZ_BLK1, CMD_TYPE_SPC_19_2MHZ_BLK2, CMD_TYPE_SPC_19_2MHZ_BLK3, CMD_TYPE_SPC_START};
  uint8_t SPCTestCmdSeq_SN220[] = {CMD_TYPE_CORE_RESET, CMD_TYPE_CORE_INIT,
          CMD_TYPE_NFCC_ALLOW_CHANGE_PARAM, CMD_TYPE_NXP_PROP_EXT, CMD_TYPE_NFCC_STANDBY_OFF, CMD_TYPE_SPC_NTF_EN_SN220,
          CMD_TYPE_SPC_19_2MHZ_BLK1_SN220, CMD_TYPE_SPC_19_2MHZ_BLK2_SN220, CMD_TYPE_SPC_19_2MHZ_BLK3_SN220, CMD_TYPE_SPC_START};
  uint8_t SPC26MHzTestCmdSeq_SN220[] = {CMD_TYPE_CORE_RESET, CMD_TYPE_CORE_INIT,
          CMD_TYPE_NFCC_ALLOW_CHANGE_PARAM, CMD_TYPE_NXP_PROP_EXT, CMD_TYPE_NFCC_STANDBY_OFF, CMD_TYPE_SPC_NTF_EN_SN220,
          CMD_TYPE_SPC_26MHZ_BLK1_SN220, CMD_TYPE_SPC_26MHZ_BLK2_SN220, CMD_TYPE_SPC_26MHZ_BLK3_SN220, CMD_TYPE_SPC_START};
  uint8_t SPC38_4MHzTestCmdSeq_SN220[] = {CMD_TYPE_CORE_RESET, CMD_TYPE_CORE_INIT,
          CMD_TYPE_NFCC_ALLOW_CHANGE_PARAM, CMD_TYPE_NXP_PROP_EXT, CMD_TYPE_NFCC_STANDBY_OFF, CMD_TYPE_SPC_NTF_EN_SN220,
          CMD_TYPE_SPC_38_4MHZ_BLK1_SN220, CMD_TYPE_SPC_38_4MHZ_BLK2_SN220, CMD_TYPE_SPC_38_4MHZ_BLK3_SN220, CMD_TYPE_SPC_START};
  uint8_t SPCTestPostCmdSeq[] = {CMD_TYPE_SPC_ROUTE};
  uint8_t SPCTestPostCmdSeq_SN220[] = {CMD_TYPE_NFCC_STANDBY_ON};

  if(nfcManager_isNfcActive()) {
    LOG(INFO) << StringPrintf("Nfc needs to be turned off");
    return status;
  }
  if (NFA_STATUS_OK ==
          android::nfcManager_doPartialInitialize(nullptr, nullptr, NFA_FAST_BOOT_MODE)) {
    if(NFA_STATUS_OK != NFA_RegVSCback (true,nfaVSCNtfCallback)) { //Register CallBack for VS NTF
      return status;
    }

    LOG(INFO) << StringPrintf("PerformSPCTest, clk_freq(%d)", clk_freq);
    if(NFC_GetChipType() >= sn220u){
      if(clk_freq == 4)
        status = executeCmdSeq(SPC26MHzTestCmdSeq_SN220, sizeof(SPC26MHzTestCmdSeq_SN220));
      else if(clk_freq == 5)
        status = executeCmdSeq(SPC38_4MHzTestCmdSeq_SN220, sizeof(SPC38_4MHzTestCmdSeq_SN220));
      else
        status = executeCmdSeq(SPCTestCmdSeq_SN220, sizeof(SPCTestCmdSeq_SN220));
    }else{
      if(clk_freq == 4)
        status = executeCmdSeq(SPC26MHzTestCmdSeq, sizeof(SPC26MHzTestCmdSeq));
      else
        status = executeCmdSeq(SPCTestCmdSeq, sizeof(SPCTestCmdSeq));
    }

    if(NFA_STATUS_OK == status) {
      SyncEventGuard guard (sNfaVscNtfEvent);
      sNfaVscNtfEvent.wait(); //wait for NFA VS NTF to come
    }
    if(NFC_GetChipType() >= sn220u)
      status = executeCmdSeq(SPCTestPostCmdSeq_SN220, sizeof(SPCTestPostCmdSeq_SN220));
    else
      status = executeCmdSeq(SPCTestPostCmdSeq, sizeof(SPCTestPostCmdSeq));

    NFA_RegVSCback (false,nfaVSCNtfCallback); //DeRegister CallBack for VS NTF
    android::nfcManager_doPartialDeInitialize(NULL, NULL);
  }
  LOG(INFO) << StringPrintf("status=%u", status);
  return status;
}
/*******************************************************************************
 ** Writes sequence of commands provided to NFCC
 ** @param *aCmdType- pointer for the list of command types
 **        aNumOfCmds- is the number of command types in the list
 ** @return status SUCCESS or FAILED.
 *******************************************************************************/
tNFA_STATUS NfcSelfTest::executeCmdSeq(uint8_t* aCmdType, uint8_t aNumOfCmds) {
  tNFA_STATUS status = NFA_STATUS_FAILED;
  uint8_t count = 0, cmdLen = 0;
  uint8_t cmdBuf[NCI_MAX_CMD_BUFFER] = {0x00};
  do {
    cmdLen = GetCmdBuffer(cmdBuf, aCmdType[count]);
    if (cmdLen == 0) {
      status = NFA_STATUS_FAILED;
      LOG(ERROR) << StringPrintf("Failed to get command buffer");
    } else {
      if (gselfTestData.fSetResFreq &&
          (CMD_TYPE_CORE_GET_CONFIG_RFTXCFG0 <= aCmdType[count] &&
           CMD_TYPE_CORE_GET_CONFIG_RFTXCFG5 >= aCmdType[count])) {
        gselfTestData.copyData = true;
      }

      SyncEventGuard guard(gselfTestData.NxpSelfTestEvt);
      status = NFA_SendRawVsCommand(cmdLen, cmdBuf, NxpResponse_SelfTest_Cb);
      if (status == NFA_STATUS_OK &&
          gselfTestData.NxpSelfTestEvt.wait(RESONANT_FREQ_CMD_WAIT)) {
        if (aCmdType[count] == CMD_TYPE_CORE_RESET) {
          usleep(1000 * 100);
        }
        LOG(INFO) << StringPrintf("Command Success");
      } else {
        SetSelfTestCbStatus(NFA_STATUS_FAILED);
        status = NFA_STATUS_FAILED; /* Response Timeout: break the loop */
        LOG(ERROR) << StringPrintf("Command Failed");
      }
    }
    /*Loop Break: 1. GetCmdBuffer() failed to get command.
     *            2. NFA_SendRawVsCommand() Failed to send command
     *            3. Response timeout 4. Response STATUS_FAILED
     *            5. Command sequence is over
     *            */
  } while ((status == NFA_STATUS_OK) &&
           (NFA_STATUS_OK == GetSelfTestCbStatus()) && (++count < aNumOfCmds));
  return GetSelfTestCbStatus();
}
