zephyr_library()

zephyr_library_include_directories(${ZEPHYR_BASE}/drivers/modem)
#add_subdirectory(drivers)

#zephyr_library_sources_ifdef(CONFIG_MODEM_RECEIVER ${ZEPHYR_BASE}/drivers/modem/modem_receiver.c)
#zephyr_library_sources_ifdef(CONFIG_MODEM_SHELL ${ZEPHYR_BASE}/drivers/modem/modem_shell.c)
#zephyr_library_sources_ifdef(CONFIG_MODEM_CONTEXT ${ZEPHYR_BASE}/drivers/modem/modem_context.c)
#zephyr_library_sources_ifdef(CONFIG_MODEM_IFACE_UART_INTERRUPT ${ZEPHYR_BASE}/drivers/modem/modem_iface_uart_interrupt.c)
#zephyr_library_sources_ifdef(CONFIG_MODEM_IFACE_UART_ASYNC ${ZEPHYR_BASE}/drivers/modem/modem_iface_uart_async.c)
#zephyr_library_sources_ifdef(CONFIG_MODEM_CMD_HANDLER ${ZEPHYR_BASE}/drivers/modem/modem_cmd_handler.c)
#zephyr_library_sources_ifdef(CONFIG_MODEM_SOCKET ${ZEPHYR_BASE}/drivers/modem/modem_socket.c)

if(CONFIG_MODEM_QUECTEL_BG96)
	zephyr_library_include_directories(${ZEPHYR_BASE}/subsys/net/ip)
	zephyr_library_sources(quectel-bg96.c)
endif()
