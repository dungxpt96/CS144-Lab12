/******************************************************************************
 * ctcp.c
 * ------
 * Implementation of cTCP done here. This is the only file you need to change.
 * Look at the following files for references and useful functions:
 *   - ctcp.h: Headers for this file.
 *   - ctcp_iinked_list.h: Linked list functions for managing a linked list.
 *   - ctcp_sys.h: Connection-related structs and functions, cTCP segment
 *           definition.
 *   - ctcp_utils.h: Checksum computation, getting the current time.
 *
 *****************************************************************************/
#include "ctcp.h"
#include "ctcp_linked_list.h"
#include "ctcp_sys.h"
#include "ctcp_utils.h"

#define NUM_RETRANSMIT 5

typedef enum
{
  CLOSE,        
  LISTEN,       
  SYN_RCVD, 
  SYN_SENT,   
  ESTABLISHED,
  CLOSE_WAIT,   
  LAST_ACK,   
  FIN_WAIT_1,   
  FIN_WAIT_2,   
  CLOSING,  
  TIME_WAIT 
} state_transition;
/**
 * Connection state.
 *
 * Stores per-connection information such as the current sequence number,
 * unacknowledged packets, etc.
 *
 * You should add to this to store other fields you might need.
 */
typedef struct node_segment
{
  ctcp_segment_t *segment;
  int is_ack_waiting;
  int segment_timeout;
  int timeout_number;
} node_segment_t;

struct ctcp_state 
{
  struct ctcp_state *next;  /* Next in linked list */
  struct ctcp_state **prev; /* Prev in linked list */

  conn_t *conn;       /* Connection object -- needed in order to figure
                 out destination when sending */
  linked_list_t *segments;  /* Linked list of segments sent to this connection.
                 It may be useful to have multiple linked lists
                 for unacknowledged segments, segments that
                 haven't been sent, etc. Lab 1 uses the
                 stop-and-wait protocol and therefore does not
                 necessarily need a linked list. You may remove
                 this if this is the case for you */

  /* FIXME: Add other needed fields. */
  state_transition tcp_state;
  node_segment_t *segment_out;
  node_segment_t *segment_in;
  uint32_t ackno;
  uint32_t seqno;
  /*int is_ack_waiting;
  int segment_timeout;
  int timeout_number;*/
  int send_window;
  int recv_window;
  linked_list_t *ll_segment_out;
  linked_list_t *ll_segment_in;
  uint32_t last_ack;
};
/**
 * Linked list of connection states. Go through this in ctcp_timer() to
 * resubmit segments and tear down connections.
 */
static ctcp_state_t *state_list;

/* FIXME: Feel free to add as many helper functions as needed. Don't repeat
      code! Helper functions make the code clearer and cleaner. */
static char buf_input[MAX_SEG_DATA_SIZE];
static int cfg_timer;
static int cfg_rt_timeout;
static int send_window_number;
static int recv_window_number;

static int __segment_send(ctcp_state_t *state, uint32_t flag, uint16_t data_len, char *data);
static int __corruption_segment_check(ctcp_segment_t *segment, uint16_t data_len);
static int __segment_save(ctcp_state_t *state);
static int __output(ctcp_state_t *state);
static int __ack_send(ctcp_state_t *state);
static int __ack_recv(ctcp_state_t *state, ctcp_segment_t *segment);

ctcp_state_t *ctcp_init(conn_t *conn, ctcp_config_t *cfg) 
{
  /* Connection could not be established. */
  if (conn == NULL) 
  {
    return NULL;
  }
  /* Established a connection. Create a new state and update the linked list
  of connection states. */
  ctcp_state_t *state = calloc(sizeof(ctcp_state_t), 1);
  state->next = state_list;
  state->prev = &state_list;
  if (state_list)
    state_list->prev = &state->next;
  state_list = state;

  /* Set fields. */
  state->conn = conn;
  /* FIXME: Do any other initialization here. */
  cfg_timer = cfg->timer;
  cfg_rt_timeout = cfg->rt_timeout;
  send_window_number = cfg->send_window / MAX_SEG_DATA_SIZE;
  recv_window_number = cfg->recv_window / MAX_SEG_DATA_SIZE;
  state->tcp_state = ESTABLISHED;
  state->ackno = 1;
  state->seqno = 1;
  /*state->is_ack_waiting = 0;
  state->timeout_number = NUM_RETRANSMIT;
  state->segment_timeout = cfg_rt_timeout;*/
  state->send_window = send_window_number;
  state->recv_window = recv_window_number;
  state->last_ack = 1;
  state->ll_segment_out = ll_create();
  state->ll_segment_in = ll_create();
  state->segment_out = calloc(1, sizeof(node_segment_t));
  if (NULL == state->segment_out)
    return NULL;
  state->segment_in = calloc(1, sizeof(node_segment_t));
  if (NULL == state->segment_in)
    return NULL;
  return state;
}

void ctcp_destroy(ctcp_state_t *state) {
  /* Update linked list. */
  if (state->next)
    state->next->prev = state->prev;

  *state->prev = state->next;
  conn_remove(state->conn);

  /* FIXME: Do any other cleanup here. */
  free(state);
  end_client();
}

void ctcp_read(ctcp_state_t *state) {
  /* FIXME */
  int ret_conn_input;
  if (state->send_window < 1)
    return;

  state->segment_out = calloc(1, sizeof(node_segment_t));
  if (NULL == state->segment_out)
    return;
  switch(state->tcp_state)
  {
    case ESTABLISHED:
      ret_conn_input = conn_input(state->conn, buf_input, sizeof(buf_input));
      if (-1 == ret_conn_input)
      {
        state->tcp_state = FIN_WAIT_1;
        if ((ret_conn_input = __segment_send(state, FIN, 0, NULL)) < 0)
          return;
      }
      else if (ret_conn_input > 0)
      {
        if ((ret_conn_input = __segment_send(state, ACK, ret_conn_input, buf_input)) < 0)
          return;
      }
      break;
    default:
      break;
  }
  if (NULL != state->segment_out)
    free(state->segment_out);
}

void ctcp_receive(ctcp_state_t *state, ctcp_segment_t *segment, size_t len) {
  /* FIXME */
  uint16_t data_len = len - sizeof(ctcp_segment_t);
  if ((size_t)ntohs(segment->len) != len)
  {
    if(NULL != segment)
    free(segment);
    return;
  }
  
  if (__corruption_segment_check(segment, len))
  {
    if(NULL != segment)
    free(segment);
    return;
  }

  switch(state->tcp_state)
  {
    case ESTABLISHED:
      if (ntohl(segment->flags) == ACK)
      {
        if (0 == data_len)
        {
          __ack_recv(state, segment);
          break;
        }

        if (state->recv_window < 1)
          return;
        if (state->ackno > ntohl(segment->seqno))
          break;
        state->recv_window--;
        if (state->last_ack == ntohl(segment->seqno))
        {
          state->segment_in->segment = calloc(1, len);
          if (NULL == state->segment_in)
            break;
          memcpy(state->segment_in->segment, segment, len);
          __output(state);

          if (__ack_send(state) < 0)
            break;
        }
        else
        {
          state->segment_in->segment = calloc(1, len);
          if (NULL == state->segment_in)
            break;
          memcpy(state->segment_in->segment, segment, len);
          __segment_save(state);
          __ack_send(state);
        }
      }
      else if (ntohl(segment->flags) == FIN)
      {
        //conn_output(state->conn, NULL, 0);
        state->segment_in->segment = calloc(1, len);
        if (NULL == state->segment_in)
          break;
        memcpy(state->segment_in->segment, segment, len);
        ctcp_output(state);
        state->ackno = ntohl(state->segment_in->segment->seqno) + 1;
        state->segment_in->segment->seqno = htonl(state->ackno);
        if (__ack_send(state) < 0)
          break;
        state->tcp_state = CLOSE_WAIT;
        
        if (__segment_send(state, FIN, 0, NULL) < 0)
          break;
        state->tcp_state = LAST_ACK;
      }
      break;
    case FIN_WAIT_1:
      if (ntohl(segment->flags) == ACK && (state->seqno + 1) == ntohl(segment->ackno))
        state->tcp_state = FIN_WAIT_2;
      else if (ntohl(segment->flags) == FIN)
      {
        state->ackno++;
        if (__segment_send(state, ACK, 0, NULL) < 0)
          break;
        state->tcp_state = CLOSING;
      }
      break;
    case FIN_WAIT_2:
      if (ntohl(segment->flags) != FIN)
        break;
      state->ackno++;
      if (__segment_send(state, ACK, 0, NULL) < 0)
        break;
      state->segment_in->segment = calloc(1, len);
      if (NULL == segment)
        break;
      memcpy(state->segment_in->segment, segment, len);
      ctcp_output(state);
      state->tcp_state = TIME_WAIT;
      state->segment_in->timeout_number = NUM_RETRANSMIT;
      state->segment_in->segment_timeout = cfg_rt_timeout * 35;
      break;  
    case CLOSING:
      if (ntohl(segment->flags) != ACK || (state->seqno + 1) == ntohl(segment->ackno))
        break;
      state->tcp_state = TIME_WAIT;
      state->segment_in->timeout_number = NUM_RETRANSMIT;
      state->segment_in->segment_timeout = cfg_rt_timeout * 35;
      break;
    case LAST_ACK:
      if (ntohl(segment->flags) != ACK || (state->seqno + 1) != ntohl(segment->ackno))
        break;
      ctcp_destroy(state);
      break;
    default:
      break;
  }
  if(NULL != segment)
    free(segment);
}

void ctcp_output(ctcp_state_t *state) {
  /* FIXME */
  uint16_t data_len = ntohs(state->segment_in->segment->len) - sizeof(ctcp_segment_t);
  if (conn_bufspace(state->conn) < data_len)
    return;
  if (0 > conn_output(state->conn, state->segment_in->segment->data, data_len))
    return;
}

void ctcp_timer() {
  /* FIXME */
  int ret;
  ctcp_state_t *state = state_list;
  if (NULL == state)
    return;
  
  if (TIME_WAIT == state->tcp_state)
  {   
    state->segment_in->segment_timeout -= cfg_timer;
    if (state->segment_in->segment_timeout > 0)
      return;
    
    state->segment_in->timeout_number--;
    if (0 == state->segment_in->timeout_number)
    {
      ctcp_destroy(state);
      return;
    }
  }
  ll_node_t *curr;
  node_segment_t *curr_note;
  curr = ll_front(state->ll_segment_out);
  while(NULL != curr)
  {
    curr_note = (node_segment_t *) curr->object;
    if (0 == curr_note->is_ack_waiting)
      return;
    
    curr_note->segment_timeout -= cfg_timer;
    if (curr_note->segment_timeout > 0)
      return;
    
    curr_note->timeout_number--;
    if (0 == curr_note->timeout_number)
    {
      ctcp_destroy(state);
      return;
    }

    switch(state->tcp_state)
    {
      case ESTABLISHED:
        ret = conn_send(state->conn, curr_note->segment, ntohs(curr_note->segment->len));
        if (ret < 0)
          break;
        curr_note->segment_timeout = cfg_rt_timeout;
        break;
      default:
        break;
    }
  }
}

static int __segment_send(ctcp_state_t *state, uint32_t flag, uint16_t data_len, char *data)
{
  node_segment_t *node_segment_add;
  node_segment_add = calloc(1, sizeof(node_segment_t));
  if (NULL == node_segment_add)
    return -1;
  int ret;
  uint16_t len = data_len + sizeof(ctcp_segment_t); // + 1 for null
  ctcp_segment_t *segment;
  segment = calloc(1, len);
  if (NULL == segment)
    return -1;
  
  segment->ackno = htonl(state->ackno);
  segment->seqno = htonl(state->seqno);
  state->seqno += data_len;
  segment->len = htons(len);
  segment->flags = htonl(flag);
  segment->window = htons(MAX_SEG_DATA_SIZE);
  memcpy(segment->data, data, data_len);
  segment->cksum = 0;
  segment->cksum = cksum(segment, len);
  
  ret = conn_send(state->conn, segment, len);
  node_segment_add->segment = calloc(1, len);
  memcpy(node_segment_add->segment, segment, len);

  state->send_window--;
  node_segment_add->is_ack_waiting = 1;
  node_segment_add->timeout_number = NUM_RETRANSMIT;
  node_segment_add->segment_timeout = cfg_rt_timeout;
  ll_add(state->ll_segment_out, node_segment_add);

  if(NULL != segment)
    free(segment);
  return ret;
}

static int __corruption_segment_check(ctcp_segment_t *segment, uint16_t len)
{
  uint16_t cksum_segment = segment->cksum;
  uint16_t segment_len = ntohs(segment->len);
 
  segment->cksum = 0;
  segment->cksum = cksum(segment, segment_len);
 
  if (segment->cksum != cksum_segment)
    return 1;
  return 0;
}


static int __segment_save(ctcp_state_t *state)
{
  ll_node_t *curr = ll_front(state->ll_segment_in);
  ctcp_segment_t *curr_segment;
  if (NULL == curr)
  {
    ll_add(state->ll_segment_in, state->segment_in);
    return 0;
  }
  
  while (NULL != curr)
  {
    curr_segment = (ctcp_segment_t *) curr->object;
    if (ntohl(state->segment_in->segment->seqno) < ntohl(curr_segment->seqno))
    {
      ll_add_after(state->ll_segment_in, curr->prev, state->segment_in);
      return 0;
    }
    else
      curr = curr->next;
  }
  ll_add(state->ll_segment_in, state->segment_in);
  return 0;
}

static int __output(ctcp_state_t *state)
{
  uint16_t data_len = ntohs(state->segment_in->segment->len) - sizeof(ctcp_segment_t);
  ctcp_segment_t *curr_segment;
  ctcp_output(state);
  state->recv_window++;
  state->last_ack += data_len;
  ll_node_t *curr = ll_front(state->ll_segment_in);
  if (NULL == curr)
    return 0;

  while (NULL != curr)
  {
    curr_segment = (ctcp_segment_t *) curr->object;
    if (state->last_ack == ntohl(curr_segment->seqno))
    {
      data_len = ntohs(curr_segment->len) - sizeof(ctcp_segment_t);
      if (conn_bufspace(state->conn) < data_len)
        return -1;
      if (0 > conn_output(state->conn, curr_segment->data, data_len))
        return -1;
      state->recv_window++;
      state->last_ack += data_len;
      if (NULL != curr->next)
      {
        curr = curr->next;
        ll_remove(state->ll_segment_in, curr->prev);
      }
      else
      {
        ll_remove(state->ll_segment_in, curr);  
        break;
      }
    }
    else
      break;
  }
  return 0;
}

static int __ack_send(ctcp_state_t *state)
{
  int ret;
  uint16_t len = sizeof(ctcp_segment_t);
  ctcp_segment_t *segment;
  segment = calloc(1, len);
  if (NULL == segment)
    return -1;
  uint32_t ack_temp = ntohl(state->segment_in->segment->seqno) + 
              ntohs(state->segment_in->segment->len) - 
              sizeof(ctcp_segment_t);
  segment->ackno = htonl(ack_temp);
  segment->seqno = htonl(state->seqno);
  segment->len = htons(len);
  segment->flags = htonl(ACK);
  segment->window = htons(MAX_SEG_DATA_SIZE);
  segment->cksum = 0;
  segment->cksum = cksum(segment, len);
  ret = conn_send(state->conn, segment, len);
  return ret;
}

static int __ack_recv(ctcp_state_t *state, ctcp_segment_t *segment)
{
  ll_node_t *curr;
  node_segment_t *curr_note;
  uint16_t data_len;
  curr = ll_front(state->ll_segment_out);
  curr_note = (node_segment_t *) curr->object;
  data_len = ntohs(curr_note->segment->len) - sizeof(ctcp_segment_t);
  if ((ntohl(curr_note->segment->seqno) + data_len) == ntohl(segment->ackno))
  {
    state->send_window++;
    if (NULL != curr->next)
    {
      curr = curr->next;
      ll_remove(state->ll_segment_out, curr->prev);
    }
    else
    {
      ll_remove(state->ll_segment_out, curr); 
      return 0;
    }
    while(NULL != curr)
    {
      if (0 == curr_note->is_ack_waiting)
      {
        state->send_window++;
        if (NULL != curr->next)
        {
          curr = curr->next;
          ll_remove(state->ll_segment_out, curr->prev);
        }
        else
        {
          ll_remove(state->ll_segment_out, curr); 
          return 0;
        }
      }
      else
        break;
    }
  }
  else
  {
    curr = curr->next;
    while(NULL != curr)
    {
      data_len = ntohs(curr_note->segment->len) - sizeof(ctcp_segment_t);
      if ((ntohl(curr_note->segment->seqno) + data_len) == ntohl(segment->ackno))
      {
        curr_note->is_ack_waiting = 0;
        break;
      }
      curr = curr->next;
    }
  } 
  return 0;
}
