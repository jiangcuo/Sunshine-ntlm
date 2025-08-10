/**
 * @file src/utility_board_uuid.cpp
 * @brief Implementation for cross-platform motherboard UUID retrieval
 */

#include "utility.h"
#include "logging.h"

// standard includes
#include <fstream>
#include <algorithm>
#include <cctype>

#ifdef _WIN32
  #include <windows.h>
  #include <comdef.h>
  #include <wbemidl.h>
  #pragma comment(lib, "wbemuuid.lib")
#endif

namespace util {
namespace board_uuid {

  // Constants for UUID formats
  const std::string ERROR_UUID = "FFFFFFFF-FFFF-FFFF-FFFF-FFFFFFFFFFFF";
  const std::string MACOS_UUID = "00000000-0000-0000-0000-000000000000";
  const std::string LINUX_DMI_PATH = "/sys/devices/virtual/dmi/id/board_serial";

  /**
   * @brief Check if string is valid UUID format and return standardized version
   */
  std::string validate_and_format_uuid(const std::string& input) {
    if (input.empty()) {
      BOOST_LOG(warning) << "[BOARD_UUID] Empty input provided, returning error UUID";
      return ERROR_UUID;
    }

    // Remove whitespace and convert to uppercase
    std::string cleaned;
    for (char c : input) {
      if (!std::isspace(c)) {
        cleaned += std::toupper(c);
      }
    }

    BOOST_LOG(debug) << "[BOARD_UUID] Cleaned input: " << cleaned;

    // Check if it's standard UUID format: XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX
    if (cleaned.length() == 36 && 
        cleaned[8] == '-' && cleaned[13] == '-' && 
        cleaned[18] == '-' && cleaned[23] == '-') {
      
      // Validate hex characters
      bool is_valid = true;
      for (size_t i = 0; i < cleaned.length(); i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) continue; // Skip dashes
        if (!std::isxdigit(cleaned[i])) {
          is_valid = false;
          break;
        }
      }
      
      if (is_valid) {
        BOOST_LOG(debug) << "[BOARD_UUID] Valid UUID format detected: " << cleaned;
        return cleaned;
      }
    }

    BOOST_LOG(warning) << "[BOARD_UUID] Invalid UUID format, returning error UUID";
    return ERROR_UUID;
  }

#ifdef __linux__
  /**
   * @brief Linux implementation - read from DMI
   */
  std::string get_board_uuid_linux() {
    BOOST_LOG(info) << "[BOARD_UUID] Linux: Reading board serial from " << LINUX_DMI_PATH;

    std::ifstream file(LINUX_DMI_PATH);
    if (!file.is_open()) {
      BOOST_LOG(warning) << "[BOARD_UUID] Linux: Failed to open DMI file: " << LINUX_DMI_PATH;
      return ERROR_UUID;
    }

    std::string serial;
    std::getline(file, serial);
    file.close();

    if (serial.empty()) {
      BOOST_LOG(warning) << "[BOARD_UUID] Linux: Empty serial read from DMI";
      return ERROR_UUID;
    }

    BOOST_LOG(info) << "[BOARD_UUID] Linux: Successfully read board serial";
    return validate_and_format_uuid(serial);
  }
#endif

#ifdef _WIN32
  /**
   * @brief Windows implementation - read via WMI
   */
  std::string get_board_uuid_windows() {
    BOOST_LOG(info) << "[BOARD_UUID] Windows: Reading board serial via WMI";

    HRESULT hres;
    
    // Initialize COM
    hres = CoInitializeEx(0, COINIT_MULTITHREADED);
    if (FAILED(hres)) {
      BOOST_LOG(error) << "[BOARD_UUID] Windows: Failed to initialize COM library. Error: " << std::hex << hres;
      return ERROR_UUID;
    }

    // Set general COM security levels
    hres = CoInitializeSecurity(
      NULL,
      -1,                          // COM negotiates service
      NULL,                        // Authentication services
      NULL,                        // Reserved
      RPC_C_AUTHN_LEVEL_NONE,      // Default authentication 
      RPC_C_IMP_LEVEL_IMPERSONATE, // Default Impersonation
      NULL,                        // Authentication info
      EOAC_NONE,                   // Additional capabilities 
      NULL                         // Reserved
    );

    if (FAILED(hres)) {
      BOOST_LOG(error) << "[BOARD_UUID] Windows: Failed to initialize security. Error: " << std::hex << hres;
      CoUninitialize();
      return ERROR_UUID;
    }

    // Obtain the initial locator to WMI
    IWbemLocator *pLoc = NULL;
    hres = CoCreateInstance(
      CLSID_WbemLocator,
      0,
      CLSCTX_INPROC_SERVER,
      IID_IWbemLocator, (LPVOID *) &pLoc);

    if (FAILED(hres)) {
      BOOST_LOG(error) << "[BOARD_UUID] Windows: Failed to create IWbemLocator object. Error: " << std::hex << hres;
      CoUninitialize();
      return ERROR_UUID;
    }

    // Connect to WMI through the IWbemLocator::ConnectServer method
    IWbemServices *pSvc = NULL;
    hres = pLoc->ConnectServer(
      _bstr_t(L"ROOT\\CIMV2"),
      NULL,
      NULL,
      0,
      NULL,
      0,
      0,
      &pSvc
    );

    if (FAILED(hres)) {
      BOOST_LOG(error) << "[BOARD_UUID] Windows: Could not connect to WMI. Error: " << std::hex << hres;
      pLoc->Release();
      CoUninitialize();
      return ERROR_UUID;
    }

    BOOST_LOG(debug) << "[BOARD_UUID] Windows: Connected to WMI successfully";

    // Set security levels on the proxy
    hres = CoSetProxyBlanket(
      pSvc,
      RPC_C_AUTHN_WINNT,
      RPC_C_AUTHZ_NONE,
      NULL,
      RPC_C_AUTHN_LEVEL_CALL,
      RPC_C_IMP_LEVEL_IMPERSONATE,
      NULL,
      EOAC_NONE
    );

    if (FAILED(hres)) {
      BOOST_LOG(error) << "[BOARD_UUID] Windows: Could not set proxy blanket. Error: " << std::hex << hres;
      pSvc->Release();
      pLoc->Release();
      CoUninitialize();
      return ERROR_UUID;
    }

    // Use the IWbemServices pointer to make requests of WMI
    IEnumWbemClassObject* pEnumerator = NULL;
    hres = pSvc->ExecQuery(
      bstr_t("WQL"),
      bstr_t("SELECT SerialNumber FROM Win32_BaseBoard"),
      WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
      NULL,
      &pEnumerator);

    if (FAILED(hres)) {
      BOOST_LOG(error) << "[BOARD_UUID] Windows: Query for board serial failed. Error: " << std::hex << hres;
      pSvc->Release();
      pLoc->Release();
      CoUninitialize();
      return ERROR_UUID;
    }

    // Get the data from the query
    IWbemClassObject *pclsObj = NULL;
    ULONG uReturn = 0;
    std::string serial;

    while (pEnumerator) {
      HRESULT hr = pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn);

      if (0 == uReturn) {
        break;
      }

      VARIANT vtProp;
      VariantInit(&vtProp);

      // Get the value of the SerialNumber property
      hr = pclsObj->Get(L"SerialNumber", 0, &vtProp, 0, 0);
      if (SUCCEEDED(hr) && vtProp.vt == VT_BSTR && vtProp.bstrVal != NULL) {
        _bstr_t bstr(vtProp.bstrVal);
        serial = static_cast<const char*>(bstr);
        BOOST_LOG(info) << "[BOARD_UUID] Windows: Successfully retrieved board serial";
      }

      VariantClear(&vtProp);
      pclsObj->Release();
      break; // Take first result
    }

    // Cleanup
    pSvc->Release();
    pLoc->Release();
    pEnumerator->Release();
    CoUninitialize();

    if (serial.empty()) {
      BOOST_LOG(warning) << "[BOARD_UUID] Windows: No board serial found via WMI";
      return ERROR_UUID;
    }

    return validate_and_format_uuid(serial);
  }
#endif

#ifdef __APPLE__
  /**
   * @brief macOS implementation - return fixed UUID
   */
  std::string get_board_uuid_macos() {
    BOOST_LOG(info) << "[BOARD_UUID] macOS: Returning fixed UUID as requested";
    return MACOS_UUID;
  }
#endif

  /**
   * @brief Main function to get board UUID across platforms
   */
  std::string get_board_uuid() {
    BOOST_LOG(info) << "[BOARD_UUID] Getting motherboard UUID...";

    try {
#ifdef __linux__
      return get_board_uuid_linux();
#elif defined(_WIN32)
      return get_board_uuid_windows();
#elif defined(__APPLE__)
      return get_board_uuid_macos();
#else
      BOOST_LOG(warning) << "[BOARD_UUID] Unsupported platform, returning error UUID";
      return ERROR_UUID;
#endif
    } catch (const std::exception& e) {
      BOOST_LOG(error) << "[BOARD_UUID] Exception occurred: " << e.what();
      return ERROR_UUID;
    } catch (...) {
      BOOST_LOG(error) << "[BOARD_UUID] Unknown exception occurred";
      return ERROR_UUID;
    }
  }

} // namespace board_uuid
} // namespace util 