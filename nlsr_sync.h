#ifndef _NLSR_SYNC_H_
#define _NLSR_SYNC_H_

struct ccn_charbuf * make_template(int scope);
int sync_monitor(char *topo_prefix, char *slice_prefix);
int write_data_to_repo(char *data,char *name_prefix);
int create_sync_slice(char *topo_prefix, char *slice_prefix);
void process_content_from_sync(struct ccn_charbuf *content_name, struct ccn_indexbuf *components);

#endif
