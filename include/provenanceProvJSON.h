/*
*
* Author: Thomas Pasquier <tfjmp2@cl.cam.ac.uk>
*
* Copyright (C) 2015-2018 University of Cambridge, Harvard University
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2, as
* published by the Free Software Foundation.
*
*/
#ifndef __PROVENANCEPROVJSON_H
#define __PROVENANCEPROVJSON_H

void set_ProvJSON_callback( void (*fcn)(char* json) );
void flush_json( void );
void append_activity(char* json_element);
void append_agent(char* json_element);
void append_entity(char* json_element);
void append_message(char* json_element);
void append_relation(char* json_element);
void append_used(char* json_element);
void append_generated(char* json_element);
void append_informed(char* json_element);
void append_derived(char* json_element);

/* disclosing nodes and relations for provjson */
#define disclose_entity_ProvJSON(content, identifier) disclose_node_ProvJSON(ENT_DISC, content, identifier)
#define disclose_activity_ProvJSON(content, identifier) disclose_node_ProvJSON(ACT_DISC, content, identifier)
#define disclose_agent_ProvJSON(content, identifier) disclose_node_ProvJSON(AGT_DISC, content, identifier)

int disclose_node_ProvJSON(uint64_t type, const char* content, union prov_identifier* identifier);
int disclose_relation_ProvJSON(uint64_t type, union prov_identifier* sender, union prov_identifier* receiver);

/* struct to json functions */
/* TODO provide clean implementation? right now probably highly inneficient */
char* relation_to_json(struct relation_struct* e);
char* used_to_json(struct relation_struct* e);
char* generated_to_json(struct relation_struct* e);
char* informed_to_json(struct relation_struct* e);
char* derived_to_json(struct relation_struct* e);
char* disc_to_json(struct disc_node_struct* n);
char* task_to_json(struct task_prov_struct* n);
char* inode_to_json(struct inode_prov_struct* n);
char* sb_to_json(struct sb_struct* n);
char* msg_to_json(struct msg_msg_struct* n);
char* shm_to_json(struct shm_struct* n);
char* packet_to_json(struct pck_struct* n);
char* str_msg_to_json(struct str_struct* n);
char* addr_to_json(struct address_struct* n);
char* pathname_to_json(struct file_name_struct* n);
const char* prefix_json();
char* machine_description_json(char* buffer);
char* iattr_to_json(struct iattr_prov_struct* n);
char* xattr_to_json(struct xattr_prov_struct* n);
char* pckcnt_to_json(struct pckcnt_struct* n);
char* arg_to_json(struct arg_struct* n);

#endif /* __PROVENANCEPROVJSON_H */
