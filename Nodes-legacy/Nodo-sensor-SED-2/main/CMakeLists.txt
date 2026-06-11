idf_component_register(SRCS "nodo_th_auto.c"
                            "sensor_sim.c"
                            "config.c"
                            "coap_server.c"
                            "registration_client.c"
                            "push_client.c"
                       INCLUDE_DIRS "."
                       REQUIRES
                            esp_event
                            esp_netif
                            nvs_flash
                            openthread
                            vfs
                            coap)

# Forzar notificaciones Observe siempre NON (nunca CON).
# Sin esto, libcoap cambia a CON tras 5 NON y con
# COAP_OBS_MAX_FAIL=1 elimina el observer al primer ACK perdido,
# rompiendo el mecanismo de Observe.
target_compile_options(__idf_main PRIVATE -DCOAP_OBS_MAX_NON=255)
