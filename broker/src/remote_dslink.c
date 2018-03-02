#include <string.h>
#include <dslink/utils.h>
#include <broker/permission/permission.h>
#include <broker/msg/msg_subscribe.h>
#include <broker/utils.h>
#include <broker/stream.h>
#include <broker/subscription.h>
#include <broker/broker.h>
#include <broker/config.h>

#include <broker/net/ws.h>

int broker_remote_dslink_init(RemoteDSLink *link) {
    memset(link, 0, sizeof(RemoteDSLink));
    if (dslink_map_init(&link->responder_streams, dslink_map_uint32_cmp,
                        dslink_map_uint32_key_len_cal, dslink_map_hash_key) != 0
        || dslink_map_init(&link->requester_streams, dslink_map_uint32_cmp,
                        dslink_map_uint32_key_len_cal, dslink_map_hash_key) != 0
            ) {
        dslink_map_free(&link->responder_streams);
        dslink_map_free(&link->requester_streams);

        return 1;
    }
    permission_groups_init(&link->permission_groups);

    vector_init(&link->_send_queue, broker_message_merge_count, sizeof(json_t*));
    uv_prepare_init(mainLoop, &link->_process_send_queue);
    link->_process_send_queue.data = link;
    return 0;
}

void broker_remote_dslink_free(RemoteDSLink *link) {
    uv_prepare_stop(&link->_process_send_queue);
    uv_close((uv_handle_t *) &link->_process_send_queue, NULL);

    dslink_vector_foreach(&link->_send_queue) {
        json_t* obj = (json_t*)(*(void**)data);
        json_decref(obj);
    }
    dslink_vector_foreach_end();
    vector_erase_range(&link->_send_queue, 0, vector_count(&link->_send_queue));
    vector_free(&link->_send_queue);
    
    if (link->auth) {
        mbedtls_ecdh_free(&link->auth->tempKey);
        DSLINK_CHECKED_EXEC(free, (void *) link->auth->pubKey);
        dslink_free(link->auth);
    }

    link->requester_streams.locked = 1;
    dslink_map_foreach(&link->requester_streams) {
        BrokerStream *stream = entry->value->data;
        requester_stream_closed(stream, link);
        entry->value->data = NULL;
    }

    link->responder_streams.locked = 1;
    dslink_map_foreach(&link->responder_streams) {
        BrokerStream *stream = entry->value->data;
        responder_stream_closed(stream, link);
        // free the node only when resp_close_callback return TRUE
        entry->value->data = NULL;
    }

    List req_sub_to_remove;
    list_init(&req_sub_to_remove);

    if (link->node) {
        vector_free(link->node->pendingAcks);

        dslink_map_foreach(&link->node->req_sub_paths) {
            // find all subscription that doesn't use qos
            SubRequester *subreq = entry->value->data;
            if (subreq->qos <= 1) {
                dslink_list_insert(&req_sub_to_remove, subreq);
            } else if (subreq->qos >= 2) {
                broker_clear_messsage_ids(subreq);
            }
        }
        dslink_list_foreach(&req_sub_to_remove) {
            // clear non-qos subscription
            SubRequester *subreq = ((ListNode *)node)->value;
            broker_free_sub_requester(subreq);
        }
        dslink_list_free_all_nodes(&req_sub_to_remove);

        dslink_map_foreach(&link->node->req_sub_paths) {
            // find all subscription that doesn't use qos
            SubRequester *subreq = entry->value->data;
            subreq->reqSid = 0xFFFFFFFF;
        }

        dslink_map_clear(&link->node->req_sub_sids);
    }

    dslink_map_free(&link->requester_streams);
    dslink_map_free(&link->responder_streams);

    permission_groups_free(&link->permission_groups);

    if (link->pingTimerHandle) {
        uv_timer_stop(link->pingTimerHandle);
        uv_close((uv_handle_t *) link->pingTimerHandle, broker_free_handle);
    }

    dslink_free((void *) link->path);
    dslink_free(link->lastWriteTime);
    json_decref(link->linkData);

    wslay_event_context_free(link->ws);
    link->ws = NULL;
}
