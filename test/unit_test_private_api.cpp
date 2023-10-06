// #define EMULATOR_LOG
#include "unity.h"
#include "Arduino.h"
#include "Emulation.h"

#define portTICK_PERIOD_MS 1
#define vTaskDelay(x) delay(x)

#include "mocks/ESPClass.hpp"
#include "mocks/TestClient.h"
#include "ssl_client.cpp"

using namespace fakeit;

TestClient testClient; // Mocked client
sslclient_context *testContext; // Context for tests

/**
 * @brief Set the up stop ssl socket object for these tests.
 * 
 * @param ctx The sslclient_context to set up.
 * @param client The client to set up.
 */
void setup_stop_ssl_socket(sslclient_context* ctx, Client* client) {
  ctx->ssl_conf.actual_ca_chain = (mbedtls_x509_crt*) malloc(sizeof(mbedtls_x509_crt));
  ctx->ssl_conf.actual_key_cert = &dummy_cert;
  ctx->ssl_conf.ca_chain = ctx->ssl_conf.actual_ca_chain;
  ctx->ssl_conf.key_cert = ctx->ssl_conf.actual_key_cert;
}

void setUp(void) {
  ArduinoFakeReset();
  ResetEmulators();
  testClient.reset();
  testClient.returns("connected", (uint8_t)1);
  mbedtls_mock_reset_return_values();
  testContext = new sslclient_context();
}

void tearDown(void) {
  delete testContext;
  testContext = nullptr;
}

/* Test client_net_send function */

void test_client_null_context(void) {
  // Arrange
  unsigned char buf[100];
  
  // Act
  int result = client_net_send(NULL, buf, sizeof(buf));
  
  // Assert
  TEST_ASSERT_EQUAL_INT(-1, result);
} 
    
void test_client_write_succeeds(void) {
  // Arrange
  testClient.returns("write", (size_t)1024).then((size_t)1024).then((size_t)1024);
  unsigned char buf[3072]; // 3 chunks of data

  // Act
  void* clientPtr = static_cast<void*>(&testClient);
  int result = client_net_send(clientPtr, buf, sizeof(buf));

  // Assert
  TEST_ASSERT_EQUAL_INT(3072, result);
}

void test_client_write_fails(void) {
  // Arrange
  testClient.returns("write", (size_t)1024).then((size_t)1024).then((size_t)0);
  unsigned char buf[3000]; // 3 chunks of data, but it fails on the 3rd chunk

  // Act
  int result = client_net_send(&testClient, buf, sizeof(buf));

  // Assert
  TEST_ASSERT_EQUAL_INT(MBEDTLS_ERR_NET_SEND_FAILED, result);
}

void test_zero_length_buffer(void) {
  // Arrange
  unsigned char buf[1];

  // Act
  int result = client_net_send(&testClient, buf, 0);
  
  // Assert
  TEST_ASSERT_EQUAL_INT(0, result);
}

void test_single_chunk_exact(void) {
  // Arrange
  unsigned char buf[1024];
  testClient.returns("write", (size_t)1024);

  // Act
  int result = client_net_send(&testClient, buf, sizeof(buf));

  // Assert
  TEST_ASSERT_EQUAL_INT(1024, result);
}

void test_partial_write(void) {
  // Arrange
  unsigned char buf[3000];
  testClient.returns("write", (size_t)500).then((size_t)500).then((size_t)500);

  // Act
  int result = client_net_send(&testClient, buf, sizeof(buf));
  

  // Assert
  TEST_ASSERT_EQUAL_INT(1500, result); // Only half the buffer is sent
}

void test_disconnected_client(void) {
  // Arrange
  unsigned char buf[1000];
  testClient.reset();
  testClient.returns("connected", (uint8_t)0);

  // Act
  int result = client_net_send(&testClient, buf, sizeof(buf));

  // Assert
  TEST_ASSERT_EQUAL_INT(1, log_e_stub.timesCalled());
  TEST_ASSERT_EQUAL_INT(-2, result); // -2 indicates disconnected client
}

void run_client_net_send_tests(void) {
  UNITY_BEGIN();
  RUN_TEST(test_client_null_context);
  RUN_TEST(test_client_write_succeeds);
  RUN_TEST(test_client_write_fails);
  RUN_TEST(test_zero_length_buffer);
  RUN_TEST(test_single_chunk_exact);
  RUN_TEST(test_partial_write);
  RUN_TEST(test_disconnected_client);
  UNITY_END();
}

/* Test client_net_recv function */

void test_null_client_context(void) {
  // Arrange
  unsigned char buf[100];

  // Act
  int result = client_net_recv(NULL, buf, sizeof(buf));

  // Assert
  TEST_ASSERT_EQUAL_INT(-1, result);
}

void test_disconnected_client_client_net_recv(void) {
  // Arrange
  testClient.reset();
  testClient.returns("connected", (uint8_t)0);
  unsigned char buf[100];

  // Act
  int result = client_net_recv(&testClient, buf, sizeof(buf));

  // Assert
  TEST_ASSERT_EQUAL_INT(-2, result);
}

void test_successful_client_read(void) {
  // Arrange
  unsigned char buf[100];
  testClient.returns("read", (int)50);

  // Act
  int result = client_net_recv(&testClient, buf, sizeof(buf));

  // Assert
  TEST_ASSERT_EQUAL_INT(50, result);
}

void test_failed_client_read(void) {
  // Arrange
  unsigned char buf[100];
  testClient.returns("read", (int)0); // Mock a read failure

  // Act
  int result = client_net_recv(&testClient, buf, sizeof(buf));

  // Assert
  TEST_ASSERT_EQUAL_INT(0, result); // Expecting 0 as read() has failed
}

void run_client_net_recv_tests(void) {
  UNITY_BEGIN();
  RUN_TEST(test_null_client_context);
  RUN_TEST(test_disconnected_client_client_net_recv);
  RUN_TEST(test_successful_client_read);
  RUN_TEST(test_failed_client_read);
  UNITY_END();
}

/* Test handle_error function */

void test_handle_error_no_logging_on_minus_30848(void) {
  // Arrange
  int err = -30848;

  // Act
  int result = _handle_error(err, "testFunction", 123);

  // Assert
  TEST_ASSERT_EQUAL_INT(-30848, result);
  TEST_ASSERT_FALSE(log_e_stub.wasCalled());
}

void test_handle_error_logging_with_mbedtls_error_c(void) {
  // Arrange
  int err = MBEDTLS_ERR_NET_SEND_FAILED;

  // Act
  int result = _handle_error(err, "testFunction", 123);

  // Assert
  TEST_ASSERT_EQUAL_INT(-0x004E, result);
  TEST_ASSERT_TRUE(log_e_stub.wasCalled());
  TEST_ASSERT_EQUAL_INT(1, log_e_stub.timesCalled());
}

void test_handle_error_logging_without_mbedtls_error_c(void) {
  // Arrange
  int err = MBEDTLS_ERR_SSL_FEATURE_UNAVAILABLE; // Some error code not being specially handled

  // Act
  int result = _handle_error(err, "testFunction", 123);

  // Assert
  TEST_ASSERT_EQUAL_INT(err, result);
  TEST_ASSERT_TRUE(log_e_stub.wasCalled());
  TEST_ASSERT_EQUAL_INT(1, log_e_stub.timesCalled());
}

void run_handle_error_tests(void) {
  UNITY_BEGIN();
  RUN_TEST(test_handle_error_no_logging_on_minus_30848);
  RUN_TEST(test_handle_error_logging_with_mbedtls_error_c);
  RUN_TEST(test_handle_error_logging_without_mbedtls_error_c);
  UNITY_END();
}

/* Test client_net_recv_timeout function */

void test_ctx_is_null(void) {
  // Arrange
  unsigned char buf[10];
  
  // Act
  int result = client_net_recv_timeout(nullptr, buf, 10, 1000);
  
  // Assert
  TEST_ASSERT_EQUAL_INT(1, log_v_stub.timesCalled());
  TEST_ASSERT_EQUAL_INT(1, log_e_stub.timesCalled());
  TEST_ASSERT_EQUAL_INT(-1, result);
}

void test_successful_read_without_delay(void) {
  // Arrange
  testClient.returns("available", (int)10);
  testClient.returns("read", (int)10);
  unsigned char buf[10];

  // Act
  int result = client_net_recv_timeout(&testClient, buf, 10, 1000);
  
  // Assert
  TEST_ASSERT_EQUAL_INT(2, log_v_stub.timesCalled());
  TEST_ASSERT_GREATER_THAN(0, result);
}

void test_successful_read_with_delay(void) {
  // Arrange
  testClient.returns("available", (int)10);
  testClient.returns("read", (int)10);
  unsigned char buf[10];

  // Act
  int result = client_net_recv_timeout(&testClient, buf, 10, 1000);
  
  // Assert
  TEST_ASSERT_EQUAL_INT(2, log_v_stub.timesCalled());
  TEST_ASSERT_GREATER_THAN(0, result);
}

void test_read_timeout(void) {
  // Arrange
  testClient.reset();
  testClient.returns("available", (int)0);
  testClient.returns("read", (int)0);
  unsigned char buf[10];

  // Act
  int result = client_net_recv_timeout(&testClient, buf, 10, 100);
  
  // Assert
  TEST_ASSERT_EQUAL_INT(1, log_v_stub.timesCalled());
  TEST_ASSERT_EQUAL(MBEDTLS_ERR_SSL_WANT_READ, result);
}

void test_read_returns_zero(void) {
  // Arrange
  testClient.returns("available", (int)10);
  testClient.returns("read", (int)0);
  unsigned char buf[10];

  // Act
  int result = client_net_recv_timeout(&testClient, buf, 10, 1000);
  
  // Assert
  TEST_ASSERT_EQUAL_INT(1, log_v_stub.timesCalled());
  TEST_ASSERT_EQUAL(MBEDTLS_ERR_SSL_WANT_READ, result);
}

void test_len_zero(void) {
  // Arrange
  unsigned char buf[10];

  // Act
  int result = client_net_recv_timeout(&testClient, buf, 0, 1000);
  
  // Assert
  TEST_ASSERT_TRUE(log_e_stub.wasCalled());
  TEST_ASSERT_EQUAL_INT(0, result);
}

void run_client_net_recv_timeout_tests(void) {
  UNITY_BEGIN();
  RUN_TEST(test_ctx_is_null);
  RUN_TEST(test_successful_read_without_delay);
  RUN_TEST(test_successful_read_with_delay);
  RUN_TEST(test_read_timeout);
  RUN_TEST(test_read_returns_zero);
  RUN_TEST(test_len_zero);
  UNITY_END();
}

/* test ssl_init function */

void test_ssl_init_correct_initialization() {
  // Arrange / Act
  ssl_init(testContext, &testClient);
  
  // Assert
  TEST_ASSERT_EQUAL_PTR(&testClient, testContext->client);
  TEST_ASSERT_EQUAL_MEMORY(&testClient, testContext->client, sizeof(Client));
}

void test_ssl_init_mbedtls_functions_called() {
  // Arrange / Act
  ssl_init(testContext, &testClient);
  
  // Assert
  TEST_ASSERT_TRUE(mbedtls_ssl_init_stub.wasCalled());
  TEST_ASSERT_TRUE(mbedtls_ssl_config_init_stub.wasCalled());
  TEST_ASSERT_TRUE(mbedtls_ctr_drbg_init_stub.wasCalled());
}

void test_ssl_init_logging() {
  // Assert / Act
  ssl_init(testContext, &testClient);
  ArgContext args = log_v_stub.getArguments();
  
  // Assert
  TEST_ASSERT_EQUAL_STRING("Init SSL", args.resolve<std::string>(0).c_str());
}

void run_ssl_init_tests(void) {
  UNITY_BEGIN();
  RUN_TEST(test_ssl_init_correct_initialization);
  RUN_TEST(test_ssl_init_mbedtls_functions_called);
  RUN_TEST(test_ssl_init_mbedtls_functions_called);
  UNITY_END();
}

/* test data_to_read function */

void test_data_to_read_success() {
  // Arrange
  mbedtls_ssl_read_stub.returns("mbedtls_ssl_read", 5);
  mbedtls_ssl_get_bytes_avail_stub.returns("mbedtls_ssl_get_bytes_avail", (size_t)5);
  
  // Act
  int result = data_to_read(testContext);
  ArgContext args = log_d_stub.getArguments();
  
  // Assert
  // TEST_ASSERT_EQUAL_STRING("RET: 5", log_d_Args[0].c_str());
  TEST_ASSERT_TRUE(log_d_stub.timesCalled() == 2);
  TEST_ASSERT_FALSE(log_e_stub.wasCalled());
  TEST_ASSERT_EQUAL(5, result); 
}

void test_data_to_read_edge_case() {
  // Arrange
  mbedtls_ssl_read_stub.returns("mbedtls_ssl_read", MBEDTLS_ERR_SSL_WANT_READ);
  mbedtls_ssl_get_bytes_avail_stub.returns("mbedtls_ssl_get_bytes_avail", (size_t)0);
  
  // Act
  int result = data_to_read(testContext);
  ArgContext argsD = log_d_stub.getArguments();
  ArgContext argsE = log_e_stub.getArguments();
  
  // Assert
  // TEST_ASSERT_EQUAL_STRING("RET: -26880", log_d_Args[0].c_str());
  TEST_ASSERT_TRUE(log_d_stub.timesCalled() == 2);
  TEST_ASSERT_FALSE(log_e_stub.wasCalled());
  TEST_ASSERT_EQUAL(0, result);
}

void test_data_to_read_failure() {
  // Arrange
  mbedtls_ssl_read_stub.returns("mbedtls_ssl_read", MBEDTLS_ERR_NET_CONN_RESET);
  mbedtls_ssl_get_bytes_avail_stub.returns("mbedtls_ssl_get_bytes_avail", (size_t)0);
  
  // Act
  int result = data_to_read(testContext);
  ArgContext argsD = log_d_stub.getArguments();
  ArgContext argsE = log_e_stub.getArguments();
  
  // Assert
  TEST_ASSERT_TRUE(log_d_stub.timesCalled() == 2);
  TEST_ASSERT_TRUE(log_e_stub.wasCalled());
  TEST_ASSERT_EQUAL(-76, result);  // -0x004C = MBEDTLS_ERR_NET_CONN_RESET
}

void run_data_to_read_tests(void) {
  UNITY_BEGIN();
  RUN_TEST(test_data_to_read_success);
  RUN_TEST(test_data_to_read_edge_case);
  RUN_TEST(test_data_to_read_failure);
  UNITY_END();
}

/* test stop_ssl_socket function */

void test_stop_ssl_socket_success(void) {
  // Arrange
  test_client_stop_stub.reset();
  ssl_init(testContext, &testClient);
  setup_stop_ssl_socket(testContext, &testClient);
  log_v_stub.reset();
  
  // Act
  stop_ssl_socket(testContext, "rootCABuff_example", "cli_cert_example", "cli_key_example");

  // Assert
  TEST_ASSERT_TRUE(test_client_stop_stub.wasCalled());
  TEST_ASSERT_TRUE(log_v_stub.timesCalled() == 8);
  TEST_ASSERT_TRUE(mbedtls_x509_crt_free_stub.wasCalled());
  TEST_ASSERT_TRUE(mbedtls_pk_free_stub.wasCalled());
  TEST_ASSERT_TRUE(mbedtls_ssl_free_stub.wasCalled());
  TEST_ASSERT_TRUE(mbedtls_ssl_config_free_stub.wasCalled());
  TEST_ASSERT_TRUE(mbedtls_ctr_drbg_free_stub.wasCalled());
  TEST_ASSERT_TRUE(mbedtls_entropy_free_stub.wasCalled());
}

void test_stop_ssl_socket_edge_null_pointers(void) {
  // Arrange
  test_client_stop_stub.reset();
  ssl_init(testContext, &testClient);
  log_v_stub.reset();

  // Act
  stop_ssl_socket(testContext, "rootCABuff_example", "cli_cert_example", "cli_key_example");

  // Assert
  TEST_ASSERT_TRUE(test_client_stop_stub.wasCalled());
  TEST_ASSERT_TRUE(log_v_stub.timesCalled() == 6);
  TEST_ASSERT_FALSE(mbedtls_x509_crt_free_stub.wasCalled());
  TEST_ASSERT_FALSE(mbedtls_pk_free_stub.wasCalled());
  TEST_ASSERT_TRUE(mbedtls_ssl_free_stub.wasCalled());
  TEST_ASSERT_TRUE(mbedtls_ssl_config_free_stub.wasCalled());
  TEST_ASSERT_TRUE(mbedtls_ctr_drbg_free_stub.wasCalled());
  TEST_ASSERT_TRUE(mbedtls_entropy_free_stub.wasCalled());
}

void test_stop_ssl_socket_failure_will_not_double_free(void) {
  // Arrange
  test_client_stop_stub.reset();
  ssl_init(testContext, &testClient);
  testContext->client = NULL;
  log_v_stub.reset();

  // Act
  stop_ssl_socket(testContext, "rootCABuff_example", "cli_cert_example", "cli_key_example");

  // Assert
  TEST_ASSERT_FALSE(test_client_stop_stub.wasCalled());
}

void run_stop_ssl_socket_tests(void) {
  UNITY_BEGIN();
  RUN_TEST(test_stop_ssl_socket_success);
  RUN_TEST(test_stop_ssl_socket_edge_null_pointers);
  RUN_TEST(test_stop_ssl_socket_failure_will_not_double_free);
  UNITY_END();
}

/* test send_ssl_data function */

void test_send_ssl_data_successful_write(void) {
  // Arrange
  testContext->client = &testClient;
  testContext->handshake_timeout = 100;
  const uint8_t data[] = "test_data";
  int len = sizeof(data) - 1; // Excluding null terminator
  mbedtls_ssl_write_stub.returns("mbedtls_ssl_write", len);

  // Act
  int ret = send_ssl_data(testContext, data, len);

  // Assert
  TEST_ASSERT_TRUE(log_v_stub.timesCalled() == 4);
  TEST_ASSERT_TRUE(mbedtls_ssl_write_stub.wasCalled());
  TEST_ASSERT_FALSE(log_e_stub.wasCalled());
  TEST_ASSERT_EQUAL_INT(len, ret);
}

void test_send_ssl_data_want_write_then_success(void) {
  // Arrange
  testContext->client = &testClient;
  testContext->handshake_timeout = 100;
  const uint8_t data[] = "test_data";
  int len = sizeof(data) - 1; // Excluding null terminator

  // First two calls to mbedtls_ssl_write will return WANT_WRITE, then it will succeed
  mbedtls_ssl_write_stub.returns("mbedtls_ssl_write", MBEDTLS_ERR_SSL_WANT_WRITE)
    .then(MBEDTLS_ERR_SSL_WANT_WRITE)
    .then(len);

  // Act
  int ret = send_ssl_data(testContext, data, len);

  // Assert
  TEST_ASSERT_TRUE(log_v_stub.timesCalled() == 4);
  TEST_ASSERT_TRUE(mbedtls_ssl_write_stub.wasCalled());
  TEST_ASSERT_FALSE(log_e_stub.wasCalled());
  TEST_ASSERT_EQUAL_INT(len, ret);
}

void test_send_ssl_data_null_context(void) {
  // Act
  int ret = send_ssl_data(NULL, NULL, 0);

  // Assert
  TEST_ASSERT_FALSE(log_v_stub.wasCalled());
  TEST_ASSERT_TRUE(mbedtls_ssl_write_stub.timesCalled() == 0);
  TEST_ASSERT_TRUE(log_e_stub.timesCalled() == 1);
  TEST_ASSERT_EQUAL_INT(-1, ret);
}

void test_send_ssl_data_mbedtls_failure(void) {
  // Arrange
  testContext->client = &testClient;
  testContext->handshake_timeout = 100;
  const uint8_t data[] = "test_data";
  int len = sizeof(data) - 1; // Excluding null terminator
  mbedtls_ssl_write_stub.returns("mbedtls_ssl_write", MBEDTLS_ERR_SSL_ALLOC_FAILED);

  // Act
  int ret = send_ssl_data(testContext, data, len);

  // Assert
  TEST_ASSERT_TRUE(ret < 0);
  TEST_ASSERT_TRUE(log_v_stub.timesCalled() == 3);
  TEST_ASSERT_TRUE(mbedtls_ssl_write_stub.wasCalled());
  TEST_ASSERT_TRUE(log_e_stub.timesCalled() == 1);
}

void test_send_ssl_data_zero_length(void) {
  // Arrange
  testContext->client = &testClient;
  testContext->handshake_timeout = 100;
  const uint8_t data[] = "test_data";
  mbedtls_ssl_write_stub.returns("mbedtls_ssl_write", 0);

  // Act
  int ret = send_ssl_data(testContext, data, 0);

  // Assert
  TEST_ASSERT_EQUAL_INT(0, ret);
  TEST_ASSERT_TRUE(mbedtls_ssl_write_stub.wasCalled());
  TEST_ASSERT_TRUE(log_v_stub.timesCalled() == 3);
  TEST_ASSERT_TRUE(log_e_stub.timesCalled() == 1);
}

void test_send_ssl_data_want_read_then_success(void) {
  // Arrange
  testContext->client = &testClient;
  testContext->handshake_timeout = 100;
  const uint8_t data[] = "test_data";
  int len = sizeof(data) - 1; // Excluding null terminator

  // First two calls to mbedtls_ssl_write will return WANT_READ, then it will succeed
  mbedtls_ssl_write_stub.returns("mbedtls_ssl_write", MBEDTLS_ERR_SSL_WANT_WRITE)
    .then(MBEDTLS_ERR_SSL_WANT_WRITE)
    .then(len);

  // Act
  int ret = send_ssl_data(testContext, data, len);

  // Assert
  TEST_ASSERT_TRUE(log_v_stub.timesCalled() == 4);
  TEST_ASSERT_TRUE(mbedtls_ssl_write_stub.wasCalled());
  TEST_ASSERT_FALSE(log_e_stub.wasCalled());
  TEST_ASSERT_EQUAL_INT(len, ret);
}

void run_send_ssl_data_tests(void) {
  UNITY_BEGIN();
  RUN_TEST(test_send_ssl_data_successful_write);
  RUN_TEST(test_send_ssl_data_want_write_then_success);
  RUN_TEST(test_send_ssl_data_null_context);
  RUN_TEST(test_send_ssl_data_mbedtls_failure);
  RUN_TEST(test_send_ssl_data_zero_length);
  RUN_TEST(test_send_ssl_data_want_read_then_success);
  UNITY_END();
}

/* Test get_ssl_receive function */

void test_get_ssl_receive_success(void) {
  // Arrange
  unsigned char data[1024];
  mbedtls_ssl_read_stub.returns("mbedtls_ssl_read", 1024);

  // Act
  int result = get_ssl_receive(testContext, data, sizeof(data));

  // Assert
  TEST_ASSERT_EQUAL_INT(1024, result);
}

void test_get_ssl_receive_partial_read(void) {
  // Arrange
  unsigned char data[1024];
  mbedtls_ssl_read_stub.returns("mbedtls_ssl_read", 512);

  // Act
  int result = get_ssl_receive(testContext, data, sizeof(data));

  // Assert
  TEST_ASSERT_EQUAL_INT(512, result);
}

void test_get_ssl_receive_failure_bad_input(void) {
  // Arrange
  unsigned char data[1024];
  mbedtls_ssl_read_stub.returns("mbedtls_ssl_read", MBEDTLS_ERR_SSL_BAD_INPUT_DATA);

  // Act
  int result = get_ssl_receive(testContext, data, sizeof(data));

  // Assert
  TEST_ASSERT_EQUAL_INT(MBEDTLS_ERR_SSL_BAD_INPUT_DATA, result);
}

void test_get_ssl_receive_failed_alloc(void) {
  // Arrange
  unsigned char data[1024];
  mbedtls_ssl_read_stub.returns("mbedtls_ssl_read", MBEDTLS_ERR_SSL_ALLOC_FAILED);

  // Act
  int result = get_ssl_receive(testContext, data, sizeof(data));

  // Assert
  TEST_ASSERT_EQUAL_INT(MBEDTLS_ERR_SSL_ALLOC_FAILED, result);
}

void test_get_ssl_receive_zero_length(void) {
  // Arrange
  unsigned char data[1];
  mbedtls_ssl_read_stub.returns("mbedtls_ssl_read", 0);

  // Act
  int result = get_ssl_receive(testContext, data, 0);

  // Assert
  TEST_ASSERT_EQUAL_INT(0, result);
}

void run_get_ssl_receive_tests(void) {
  UNITY_BEGIN();
  RUN_TEST(test_get_ssl_receive_success);
  RUN_TEST(test_get_ssl_receive_partial_read);
  RUN_TEST(test_get_ssl_receive_failure_bad_input);
  RUN_TEST(test_get_ssl_receive_failed_alloc);
  RUN_TEST(test_get_ssl_receive_zero_length);
  UNITY_END();
}

/* test parse_hex_nibble function */

void test_parse_hex_nibble_digit(void) {
  // Arrange
  uint8_t result;

  // Act
  bool success = parse_hex_nibble('5', &result);

  // Assert
  TEST_ASSERT_TRUE(success);
  TEST_ASSERT_EQUAL_UINT8(5, result);
}

void test_parse_hex_nibble_lowercase(void) {
  // Arrange
  uint8_t result;

  // Act
  bool success = parse_hex_nibble('b', &result);

  // Assert
  TEST_ASSERT_TRUE(success);
  TEST_ASSERT_EQUAL_UINT8(11, result);
}

void test_parse_hex_nibble_uppercase(void) {
  // Arrange
  uint8_t result;

  // Act
  bool success = parse_hex_nibble('D', &result);

  // Assert
  TEST_ASSERT_TRUE(success);
  TEST_ASSERT_EQUAL_UINT8(13, result);
}

void test_parse_hex_nibble_below_range(void) {
  // Arrange
  uint8_t result;

  // Act
  bool success = parse_hex_nibble('/', &result);

  // Assert
  TEST_ASSERT_FALSE(success);
}

void test_parse_hex_nibble_between_range(void) {
  // Arrange
  uint8_t result;

  // Act
  bool success = parse_hex_nibble('h', &result);

  // Assert
  TEST_ASSERT_FALSE(success);
}

void test_parse_hex_nibble_above_range(void) {
  // Arrange
  uint8_t result;

  // Act
  bool success = parse_hex_nibble('Z', &result);

  // Assert
  TEST_ASSERT_FALSE(success);
}

void test_parse_hex_nibble_edge_smallest(void) {
  // Arrange
  uint8_t result;

  // Act
  bool success = parse_hex_nibble('0', &result);

  // Assert
  TEST_ASSERT_TRUE(success);
  TEST_ASSERT_EQUAL_UINT8(0, result);
}

void test_parse_hex_nibble_edge_largest(void) {
  // Arrange
  uint8_t result;

  // Act
  bool success = parse_hex_nibble('f', &result);

  // Assert
  TEST_ASSERT_TRUE(success);
  TEST_ASSERT_EQUAL_UINT8(15, result);
}

void run_parse_hex_nibble_tests(void) {
  UNITY_BEGIN();
  RUN_TEST(test_parse_hex_nibble_digit);
  RUN_TEST(test_parse_hex_nibble_lowercase);
  RUN_TEST(test_parse_hex_nibble_uppercase);
  RUN_TEST(test_parse_hex_nibble_below_range);
  RUN_TEST(test_parse_hex_nibble_between_range);
  RUN_TEST(test_parse_hex_nibble_above_range);
  RUN_TEST(test_parse_hex_nibble_edge_smallest);
  RUN_TEST(test_parse_hex_nibble_edge_largest);
  UNITY_END();
}

/* test match_name function */

void test_match_name_exact_match(void) {
  // Arrange
  string name = "example.com";
  string domainName = "example.com";

  // Act
  bool result = match_name(name, domainName);

  // Assert
  TEST_ASSERT_TRUE(result);
}

void test_match_name_simple_wildcard_match(void) {
  // Arrange
  string name = "*.example.com";
  string domainName = "test.example.com";

  // Act
  bool result = match_name(name, domainName);

  // Assert
  TEST_ASSERT_TRUE(result);
}

void test_match_name_exact_mismatch(void) {
  // Arrange
  string name = "example1.com";
  string domainName = "example2.com";

  // Act
  bool result = match_name(name, domainName);

  // Assert
  TEST_ASSERT_FALSE(result);
}

void test_match_name_wildcard_wrong_position(void) {
  // Arrange
  string name = "test.*.example.com";
  string domainName = "test.abc.example.com";

  // Act
  bool result = match_name(name, domainName);

  // Assert
  TEST_ASSERT_FALSE(result);
}

void test_match_name_wildcard_not_beginning(void) {
  // Arrange
  string name = "te*.example.com";
  string domainName = "test.example.com";

  // Act
  bool result = match_name(name, domainName);

  // Assert
  TEST_ASSERT_FALSE(result);
}

void test_match_name_wildcard_without_subdomain(void) {
  // Arrange
  string name = "*.example.com";
  string domainName = "example.com";

  // Act
  bool result = match_name(name, domainName);

  // Assert
  TEST_ASSERT_FALSE(result);
}

void run_match_name_tests(void) {
  UNITY_BEGIN();
  RUN_TEST(test_match_name_exact_match);
  RUN_TEST(test_match_name_simple_wildcard_match);
  RUN_TEST(test_match_name_exact_mismatch);
  RUN_TEST(test_match_name_wildcard_wrong_position);
  RUN_TEST(test_match_name_wildcard_not_beginning);
  RUN_TEST(test_match_name_wildcard_without_subdomain);
  UNITY_END();
}

/* test verify_ssl_fingerprint function */

void test_verify_ssl_fingerprint_short_fp(void) {
  // Arrange
  const char* short_fp = "d83c1c1f57";

  // Act
  bool result = verify_ssl_fingerprint(testContext, short_fp, nullptr);

  // Assert
  TEST_ASSERT_FALSE(result);
}

void test_verify_ssl_fingerprint_invalid_format(void) {
  // Arrange
  const char* invalid_fp = "invalid_format_fp";

  // Act
  bool result = verify_ssl_fingerprint(testContext, invalid_fp, nullptr);

  // Assert
  TEST_ASSERT_FALSE(result);
}

void test_verify_ssl_fingerprint_invalid_hex_sequence(void) {
  // Arrange
  const char* invalid_hex = "d83c1c1f574fd9e75a7848ad8fb131302c31e224ad8c2617a9b3e24e81fc44ez"; // 'z' is not a valid hex character

  // Act
  bool result = verify_ssl_fingerprint(testContext, invalid_hex, nullptr);

  // Assert
  TEST_ASSERT_FALSE_MESSAGE(result, "Expected invalid hex sequence to fail.");
}

void test_verify_ssl_fingerprint_domain_fail(void) {
  // Arrange
  mbedtls_ssl_get_peer_cert_stub.returns("mbedtls_ssl_get_peer_cert", &dummy_cert);

  const char* test_fp = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

  // Act
  bool result = verify_ssl_fingerprint(testContext, test_fp, "examplecom");

  // Assert
  TEST_ASSERT_FALSE(result);
}

void test_verify_ssl_fingerprint_no_peer_cert(void) {
  // Arrange
  mbedtls_ssl_get_peer_cert_stub.returns("mbedtls_ssl_get_peer_cert", &dummy_cert);
  const char* valid_fp = "d83c1c1f574fd9e75a7848ad8fb131302c31e224ad8c2617a9b3e24e81fc44e5";

  // Act
  bool result = verify_ssl_fingerprint(testContext, valid_fp, nullptr);

  // Assert
  TEST_ASSERT_FALSE(result);
}

void run_verify_ssl_fingerprint_tests(void) {
  UNITY_BEGIN();
  RUN_TEST(test_verify_ssl_fingerprint_short_fp);
  RUN_TEST(test_verify_ssl_fingerprint_invalid_format);
  RUN_TEST(test_verify_ssl_fingerprint_invalid_hex_sequence);
  RUN_TEST(test_verify_ssl_fingerprint_domain_fail);
  RUN_TEST(test_verify_ssl_fingerprint_no_peer_cert);
  UNITY_END();
}

/* test verify_ssl_dn function */

void test_verify_ssl_dn_match_in_sans(void) {
  // Arrange
  std:string domainName = "example.com";
  mbedtls_ssl_get_peer_cert_stub.returns("mbedtls_ssl_get_peer_cert", &dummy_cert_with_san);

  // Act
  bool result = verify_ssl_dn(testContext, domainName.c_str());

  // Assert
  TEST_ASSERT_TRUE_MESSAGE(result, "Expected to match domain name in SANs.");
}

void test_verify_ssl_dn_match_in_cn(void) {
  // Arrange
  std:string domainName = "example.com";
  mbedtls_ssl_get_peer_cert_stub.returns("mbedtls_ssl_get_peer_cert", &dummy_cert_with_cn);

  // Act
  bool result = verify_ssl_dn(testContext, domainName.c_str());

  // Assert
  TEST_ASSERT_TRUE_MESSAGE(result, "Expected to match domain name in CN.");
}

void test_verify_ssl_dn_no_match(void) {
  // Arrange
  std:string domainName = "example.com";
  mbedtls_ssl_get_peer_cert_stub.returns("mbedtls_ssl_get_peer_cert", &dummy_cert_without_match);

  // Act
  bool result = verify_ssl_dn(testContext, domainName.c_str());

  // Assert
  TEST_ASSERT_FALSE_MESSAGE(result, "Expected no domain name match in both SANs and CN.");
}

void test_verify_ssl_dn_empty_domain_name(void) {
  // Arrange
  std::string emptyDomainName = "";
  mbedtls_ssl_get_peer_cert_stub.returns("mbedtls_ssl_get_peer_cert", &dummy_cert_without_match);

  // Act
  bool result = verify_ssl_dn(testContext, emptyDomainName.c_str());

  // Assert
  TEST_ASSERT_FALSE_MESSAGE(result, "Expected to fail with an empty domain name.");
}

void test_verify_ssl_dn_no_peer_cert(void) {
  // Arrange
  std:string domainName = "example.com";
  mbedtls_ssl_get_peer_cert_stub.returns("mbedtls_ssl_get_peer_cert", &dummy_cert);

  // Act
  bool result = verify_ssl_dn(testContext, domainName.c_str());

  // Assert
  TEST_ASSERT_FALSE_MESSAGE(result, "Expected to fail when no peer certificate is found.");
}

void run_verify_ssl_dn_tests(void) {
  UNITY_BEGIN();
  RUN_TEST(test_verify_ssl_dn_match_in_sans);
  RUN_TEST(test_verify_ssl_dn_match_in_cn);
  RUN_TEST(test_verify_ssl_dn_no_match);
  RUN_TEST(test_verify_ssl_dn_empty_domain_name);
  RUN_TEST(test_verify_ssl_dn_no_peer_cert);
  UNITY_END();
}

/* End of test functions */

#ifdef ARDUINO

#include <Arduino.h>

void setup() {
  delay(2000); // If using Serial, allow time for serial monitor to open
  run_all_tests();
}

void loop() {
  // Empty loop
}

#else

int main(int argc, char **argv) {
  run_handle_error_tests();
  run_client_net_recv_tests();
  run_client_net_recv_timeout_tests();
  run_client_net_send_tests();
  run_ssl_init_tests();
  run_stop_ssl_socket_tests();
  run_data_to_read_tests();
  run_send_ssl_data_tests();
  run_get_ssl_receive_tests();
  run_parse_hex_nibble_tests();
  run_match_name_tests();
  run_verify_ssl_fingerprint_tests(); // We are currently not testing the fingerprint verification
  run_verify_ssl_dn_tests();
  return 0;
}

#endif
