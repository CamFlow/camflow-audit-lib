/*
*
* provenancelib.c
*
* Author: Thomas Pasquier <tfjmp2@cam.ac.uk>
*
* Copyright (C) 2015 University of Cambridge
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2, as
* published by the Free Software Foundation.
*
*/
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <netdb.h>

#include "thpool.h"
#include "provenancelib.h"

#define NUMBER_CPUS           256 /* support 256 core max */
#define PROV_BASE_NAME        "/sys/kernel/debug/provenance"
#define LONG_PROV_BASE_NAME   "/sys/kernel/debug/long_provenance"

/* internal variables */
static struct provenance_ops prov_ops;
static uint8_t ncpus;
/* per cpu variables */
static int relay_file[NUMBER_CPUS];
static int long_relay_file[NUMBER_CPUS];
/* worker pool */
static threadpool worker_thpool=NULL;

/* internal functions */
static int open_files(void);
static int close_files(void);
static int create_worker_pool(void);
static int destroy_worker_pool(void);

static void callback_job(void* data);
static void long_callback_job(void* data);
static void reader_job(void *data);
static void long_reader_job(void *data);


int provenance_register(struct provenance_ops* ops)
{
  int err;
  /* the provenance usher will not appear in trace */
  err = provenance_set_opaque(true);
  if(err)
  {
    return err;
  }
  /* copy ops function pointers */
  memcpy(&prov_ops, ops, sizeof(struct provenance_ops));

  /* count how many CPU */
  ncpus = sysconf(_SC_NPROCESSORS_ONLN);
  if(ncpus>NUMBER_CPUS){
    return -1;
  }

  /* open relay files */
  if(open_files()){
    return -1;
  }

  /* create callback threads */
  if(create_worker_pool()){
    close_files();
    return -1;
  }
  return 0;
}

void provenance_stop()
{
  close_files();
  destroy_worker_pool();
}

static int open_files(void)
{
  int i;
  char tmp[4096]; // to store file name

  for(i=0; i<ncpus; i++){
    sprintf(tmp, "%s%d", PROV_BASE_NAME, i);
    relay_file[i] = open(tmp, O_RDONLY | O_NONBLOCK);
    if(relay_file[i]<0){
      return -1;
    }
    sprintf(tmp, "%s%d", LONG_PROV_BASE_NAME, i);
    long_relay_file[i] = open(tmp, O_RDONLY | O_NONBLOCK);
    if(long_relay_file[i]<0){
      return -1;
    }
  }
  return 0;
}

static int close_files(void)
{
  int i;
  for(i=0; i<ncpus;i++){
    close(relay_file[i]);
    close(long_relay_file[i]);
  }
  return 0;
}

static int create_worker_pool(void)
{
  int i;
  uint8_t* cpunb;
  worker_thpool = thpool_init(ncpus*4);
  /* set reader jobs */
  for(i=0; i<ncpus; i++){
    cpunb = (uint8_t*)malloc(sizeof(uint8_t)); // will be freed in worker
    (*cpunb)=i;
    thpool_add_work(worker_thpool, (void*)reader_job, (void*)cpunb);
    thpool_add_work(worker_thpool, (void*)long_reader_job, (void*)cpunb);
  }
}

static int destroy_worker_pool(void)
{
  thpool_wait(worker_thpool); // wait for all jobs in queue to be finished
  thpool_destroy(worker_thpool); // destory all worker threads
}

/* per worker thread initialised variable */
static __thread int initialised=0;

/* handle application callbacks */
static void callback_job(void* data)
{
  prov_msg_t* msg = (prov_msg_t*)data;

  /* initialise per worker thread */
  if(!initialised && prov_ops.init!=NULL){
    prov_ops.init();
    initialised=1;
  }

  switch(prov_type(msg)){
    case MSG_EDGE:
      if(prov_ops.log_edge!=NULL)
        prov_ops.log_edge(&(msg->edge_info));
      break;
    case MSG_TASK:
      if(prov_ops.log_task!=NULL)
        prov_ops.log_task(&(msg->task_info));
      break;
    case MSG_INODE_UNKNOWN:
    case MSG_INODE_LINK:
    case MSG_INODE_FILE:
    case MSG_INODE_DIRECTORY:
    case MSG_INODE_CHAR:
    case MSG_INODE_BLOCK:
    case MSG_INODE_FIFO:
    case MSG_INODE_SOCKET:
      if(prov_ops.log_inode!=NULL)
        prov_ops.log_inode(&(msg->inode_info));
      break;
    case MSG_MSG:
      if(prov_ops.log_msg!=NULL)
        prov_ops.log_msg(&(msg->msg_msg_info));
      break;
    case MSG_SHM:
      if(prov_ops.log_shm!=NULL)
        prov_ops.log_shm(&(msg->shm_info));
      break;
    case MSG_SOCK:
      if(prov_ops.log_sock!=NULL)
        prov_ops.log_sock(&(msg->sock_info));
      break;
    default:
      printf("Error: unknown message type %u\n", prov_type(msg));
      break;
  }
  free(data); /* free the memory allocated in the reader */
}

/* handle application callbacks */
static void long_callback_job(void* data)
{
  long_prov_msg_t* msg = (long_prov_msg_t*)data;

  /* initialise per worker thread */
  if(!initialised && prov_ops.init!=NULL){
    prov_ops.init();
    initialised=1;
  }

  switch(prov_type(msg)){
    case MSG_STR:
      if(prov_ops.log_str!=NULL)
        prov_ops.log_str(&(msg->str_info));
      break;
    case MSG_FILE_NAME:
      if(prov_ops.log_file_name!=NULL)
        prov_ops.log_file_name(&(msg->file_name_info));
      break;
    case MSG_ADDR:
      if(prov_ops.log_address!=NULL)
        prov_ops.log_address(&(msg->address_info));
      break;
    case MSG_IFC:
      if(prov_ops.log_ifc!=NULL)
        prov_ops.log_ifc(&(msg->ifc_info));
      break;
    case MSG_DISC_ENTITY:
    case MSG_DISC_ACTIVITY:
    case MSG_DISC_AGENT:
    case MSG_DISC_NODE:
      if(prov_ops.log_disc!=NULL)
        prov_ops.log_disc(&(msg->disc_node_info));
      break;
    default:
      printf("Error: unknown message type %u\n", prov_type(msg));
      break;
  }
  free(data); /* free the memory allocated in the reader */
}

/* read from relayfs file */
static void reader_job(void *data)
{
  uint8_t* buf;
  size_t size;
  int rc;
  uint8_t cpu = (uint8_t)(*(uint8_t*)data);
  struct pollfd pollfd;

  do{
    /* file to look on */
    pollfd.fd = relay_file[cpu];
    /* something to read */
		pollfd.events = POLLIN;
    /* one file, timeout 100ms */
    rc = poll(&pollfd, 1, 100);
    if(rc<0){
      if(errno!=EINTR){
        break; /* something bad happened */
      }
    }
    buf = (uint8_t*)malloc(sizeof(prov_msg_t)); /* freed by worker thread */

    size = 0;
    do{
      rc = read(relay_file[cpu], buf+size, sizeof(prov_msg_t)-size);
      if(rc==0){ /* we did not read anything */
        continue;
      }
      if(rc<0){
        if(errno==EAGAIN){ // retry
          continue;
        }
        thpool_add_work(worker_thpool, (void*)reader_job, (void*)data);
        return; // something bad happened
      }
      size+=rc;
    }while(size<sizeof(prov_msg_t));
    /* add job to queue */
    thpool_add_work(worker_thpool, (void*)callback_job, buf);
  }while(1);
}

/* read from relayfs file */
static void long_reader_job(void *data)
{
  uint8_t* buf;
  size_t size;
  int rc;
  uint8_t cpu = (uint8_t)(*(uint8_t*)data);
  struct pollfd pollfd;

  do{
    /* file to look on */
    pollfd.fd = long_relay_file[cpu];
    /* something to read */
		pollfd.events = POLLIN;
    /* one file, timeout 100ms */
    rc = poll(&pollfd, 1, 100);
    if(rc<0){
      if(errno!=EINTR){
        break; /* something bad happened */
      }
    }
    buf = (uint8_t*)malloc(sizeof(long_prov_msg_t)); /* freed by worker thread */

    size = 0;
    do{
      rc = read(long_relay_file[cpu], buf+size, sizeof(long_prov_msg_t)-size);
      if(rc==0){ /* we did not read anything */
        continue;
      }
      if(rc<0){
        printf("Error %d\n", rc);
        if(errno==EAGAIN){ // retry
          continue;
        }
        thpool_add_work(worker_thpool, (void*)long_reader_job, (void*)data);
        return; // something bad happened
      }
      size+=rc;
    }while(size<sizeof(long_prov_msg_t));
    /* add job to queue */
    thpool_add_work(worker_thpool, (void*)long_callback_job, buf);
  }while(1);
}

int provenance_set_enable(bool value){
  int fd = open(PROV_ENABLE_FILE, O_WRONLY);

  if(fd<0)
  {
    return fd;
  }
  if(value)
  {
    write(fd, "1", sizeof(char));
  }else{
    write(fd, "0", sizeof(char));
  }
  close(fd);
  return 0;
}

bool provenance_get_enable( void ){
  int fd = open(PROV_ENABLE_FILE, O_RDONLY);
  char c;
  if(fd<0)
  {
    return false;
  }

  read(fd, &c, sizeof(char));
  close(fd);
  return c!='0';
}

int provenance_set_all(bool value){
  int fd = open(PROV_ALL_FILE, O_WRONLY);

  if(fd<0)
  {
    return fd;
  }
  if(value)
  {
    write(fd, "1", sizeof(char));
  }else{
    write(fd, "0", sizeof(char));
  }
  close(fd);
  return 0;
}

bool provenance_get_all( void ){
  int fd = open(PROV_ALL_FILE, O_RDONLY);
  char c;
  if(fd<0)
  {
    return false;
  }

  read(fd, &c, sizeof(char));
  close(fd);
  return c!='0';
}

int provenance_add_node_filter( uint32_t filter ){
  struct prov_filter f;
  int fd = open(PROV_NODE_FILTER_FILE, O_WRONLY);

  if(fd<0)
  {
    return fd;
  }
  f.filter=filter;
  f.add=1;

  write(fd, &f, sizeof(struct prov_filter));
  close(fd);
  return 0;
}

int provenance_remove_node_filter( uint32_t filter ){
  struct prov_filter f;
  int fd = open(PROV_NODE_FILTER_FILE, O_WRONLY);

  if(fd<0)
  {
    return fd;
  }
  f.filter=filter;
  f.add=0;

  write(fd, &f, sizeof(struct prov_filter));
  close(fd);
  return 0;
}

int provenance_get_node_filter( uint32_t* filter ){
  int fd = open(PROV_NODE_FILTER_FILE, O_RDONLY);
  int err=0;
  if(fd<0)
  {
    return fd;
  }

  read(fd, filter, sizeof(uint32_t));
  close(fd);
  return 0;
}

int provenance_add_edge_filter( uint32_t filter ){
  struct prov_filter f;
  int fd = open(PROV_EDGE_FILTER_FILE, O_WRONLY);

  if(fd<0)
  {
    return fd;
  }
  f.filter=filter;
  f.add=1;

  write(fd, &f, sizeof(struct prov_filter));
  close(fd);
  return 0;
}

int provenance_remove_edge_filter( uint32_t filter ){
  struct prov_filter f;
  int fd = open(PROV_EDGE_FILTER_FILE, O_WRONLY);

  if(fd<0)
  {
    return fd;
  }
  f.filter=filter;
  f.add=0;

  write(fd, &f, sizeof(struct prov_filter));
  close(fd);
  return 0;
}

int provenance_get_edge_filter( uint32_t* filter ){
  int fd = open(PROV_EDGE_FILTER_FILE, O_RDONLY);
  int err=0;
  if(fd<0)
  {
    return fd;
  }

  read(fd, filter, sizeof(uint32_t));
  close(fd);
  return 0;
}

int provenance_set_opaque(bool value){
  int fd = open(PROV_OPAQUE_FILE, O_WRONLY);

  if(fd<0)
  {
    return fd;
  }
  if(value)
  {
    write(fd, "1", sizeof(char));
  }else{
    write(fd, "0", sizeof(char));
  }
  close(fd);
  return 0;
}

int provenance_set_tracked(bool value){
  int fd = open(PROV_TRACKED_FILE, O_WRONLY);

  if(fd<0)
  {
    return fd;
  }
  if(value)
  {
    write(fd, "1", sizeof(char));
  }else{
    write(fd, "0", sizeof(char));
  }
  close(fd);
  return 0;
}

int provenance_set_machine_id(uint32_t v){
  int fd = open(PROV_MACHINE_ID_FILE, O_WRONLY);

  if(fd<0)
  {
    return fd;
  }
  write(fd, &v, sizeof(uint32_t));
  close(fd);
  return 0;
}

int provenance_get_machine_id(uint32_t* v){
  int fd = open(PROV_MACHINE_ID_FILE, O_WRONLY);

  if(fd<0)
  {
    return fd;
  }
  read(fd, v, sizeof(uint32_t));
  close(fd);
  return 0;
}

int provenance_disclose_node(struct disc_node_struct* node){
  int rc;
  int fd = open(PROV_NODE_FILE, O_WRONLY);

  if(fd<0)
  {
    return fd;
  }
  rc = write(fd, node, sizeof(struct disc_node_struct));
  close(fd);
  return rc;
}

int provenance_disclose_edge(struct edge_struct* edge){
  int rc;
  int fd = open(PROV_EDGE_FILE, O_WRONLY);

  if(fd<0)
  {
    return fd;
  }
  rc = write(fd, edge, sizeof(struct edge_struct));
  close(fd);
  return rc;
}

int provenance_self(struct task_prov_struct* self){
  int rc;
  int fd = open(PROV_SELF_FILE, O_RDONLY);

  if(fd<0)
  {
    return fd;
  }
  rc = read(fd, self, sizeof(struct task_prov_struct));
  close(fd);
  return rc;
}

bool provenance_is_present(void){
  if(access(PROV_ENABLE_FILE, F_OK)){ // return 0 if file exists.
    return false;
  }
  return true;
}
