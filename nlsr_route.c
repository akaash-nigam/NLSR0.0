#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/time.h>
#include <assert.h>
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <sys/types.h>


#include <ccn/ccn.h>
#include <ccn/uri.h>
#include <ccn/keystore.h>
#include <ccn/signing.h>
#include <ccn/schedule.h>
#include <ccn/hashtb.h>


#include "nlsr.h"
#include "nlsr_route.h"
#include "nlsr_lsdb.h"
#include "nlsr_npt.h"
#include "nlsr_adl.h"
#include "nlsr_fib.h"
#include "utility.h"

int
route_calculate(struct ccn_schedule *sched, void *clienth, struct ccn_scheduled_event *ev, int flags)
{

	if(flags == CCN_SCHEDULE_CANCEL)
	{
 	 	return -1;
	}

	nlsr_lock();

	if ( nlsr->debugging )
		printf("route_calculate called\n");
	if ( nlsr->detailed_logging )
		writeLogg(__FILE__,__FUNCTION__,__LINE__,"route_calculate called\n");

	if( ! nlsr->is_build_adj_lsa_sheduled )
	{
		/* Calculate Route here */
		print_routing_table();
		print_npt();		

		//struct hashtb_param param_me = {0};
		nlsr->map = hashtb_create(sizeof(struct map_entry), NULL);
		nlsr->rev_map = hashtb_create(sizeof(struct map_entry), NULL);
		make_map();
		assign_mapping_number();		
		print_map();
		print_rev_map();

		do_old_routing_table_updates();
		clear_old_routing_table();	
		print_routing_table();
		print_npt();

		int i;
		int **adj_matrix;
		int map_element=hashtb_n(nlsr->map);
		adj_matrix=malloc(map_element * sizeof(int *));
		for(i = 0; i < map_element; i++)
		{
			adj_matrix[i] = malloc(map_element * sizeof(int));
		}
		make_adj_matrix(adj_matrix,map_element);
		if ( nlsr->debugging )
			print_adj_matrix(adj_matrix,map_element);

		long int source=get_mapping_no(nlsr->router_name);
		int num_link=get_no_link_from_adj_matrix(adj_matrix, map_element ,source);

		if ( nlsr->is_hyperbolic_calc == 1)
		{
			long int *links=(long int *)malloc(num_link*sizeof(long int));
			long int *link_costs=(long int *)malloc(num_link*sizeof(long int));
			get_links_from_adj_matrix(adj_matrix, map_element , links, link_costs, source);

			struct hashtb_enumerator ee;
			struct hashtb_enumerator *e = &ee;
			for (hashtb_start(nlsr->rev_map, e); e->key != NULL; hashtb_next(e)) 
			{
				struct map_entry *me=e->data;
				if ( me->mapping != source )
				{
					long int *faces=(long int *)calloc(num_link,sizeof(long int));
					double *nbr_dist=(double *)calloc(num_link,sizeof(double));
					double *nbr_to_dest=(double *)calloc(num_link,sizeof(double));
					for ( i=0 ; i < num_link; i++)
					{
						int face=get_next_hop_face_from_adl(get_router_from_rev_map(links[i]));
						double dist_to_nbr=get_hyperbolic_distance(source,links[i]);
						double dist_to_dest_from_nbr=get_hyperbolic_distance(links[i],me->mapping);
						faces[i]=face;
						nbr_dist[i]=dist_to_nbr;
						nbr_to_dest[i]=	dist_to_dest_from_nbr;	

						
					}
					sort_hyperbolic_route(nbr_to_dest,nbr_dist, faces,0,num_link);
					if (nlsr->max_faces_per_prefix == 0 )
					{
						for ( i=0 ; i < num_link; i++)
						{
							update_routing_table_with_new_hyperbolic_route(me->mapping,faces[i],nbr_to_dest[i]);
						}				
					}
					else if ( nlsr->max_faces_per_prefix > 0 )
					{
						if ( num_link <= nlsr->max_faces_per_prefix )
						{
							for ( i=0 ; i < num_link; i++)
							{
								update_routing_table_with_new_hyperbolic_route(me->mapping,faces[i],nbr_to_dest[i]);
							}
						}
						else if (num_link > nlsr->max_faces_per_prefix)
						{
							for ( i=0 ; i < nlsr->max_faces_per_prefix; i++)
							{
								update_routing_table_with_new_hyperbolic_route(me->mapping,faces[i],nbr_to_dest[i]);
							}
						}

					}
					free(faces);
					free(nbr_dist);
					free(nbr_to_dest);
				}
			}
			hashtb_end(e);

			
			free(links);
			free(link_costs);
		}
		else if (nlsr->is_hyperbolic_calc == 0 )
		{

			long int *parent=(long int *)malloc(map_element * sizeof(long int));
			long int *dist=(long int *)malloc(map_element * sizeof(long int));
			
		
			if ( (num_link == 0) || (nlsr->max_faces_per_prefix == 1 ) )
			{	
				calculate_path(adj_matrix,parent,dist, map_element, source);		
				print_all_path_from_source(parent,source);
				print_all_next_hop(parent,source);		
				update_routing_table_with_new_route(parent, dist,source);
			}
			else if ( (num_link != 0) && (nlsr->max_faces_per_prefix == 0 || nlsr->max_faces_per_prefix > 1 ) )
			{
				long int *links=(long int *)malloc(num_link*sizeof(long int));
				long int *link_costs=(long int *)malloc(num_link*sizeof(long int));
				get_links_from_adj_matrix(adj_matrix, map_element , links, link_costs, source);
				for ( i=0 ; i < num_link; i++)
				{
					adjust_adj_matrix(adj_matrix, map_element,source,links[i],link_costs[i]);
					calculate_path(adj_matrix,parent,dist, map_element, source);		
					print_all_path_from_source(parent,source);
					print_all_next_hop(parent,source);		
					update_routing_table_with_new_route(parent, dist,source);
				}

				free(links);
				free(link_costs);
			}
			free(parent);
			free(dist);
		}
		
		print_routing_table();
		print_npt();

		update_npt_with_new_route();

		print_routing_table();
		print_npt();


		for(i = 0; i < map_element; i++)
		{
			free(adj_matrix[i]);
		}
		
		free(adj_matrix);
		destroy_map();
		destroy_rev_map();
		//hashtb_destroy(&nlsr->map);
		//hashtb_destroy(&nlsr->rev_map);
		
	}
	nlsr->is_route_calculation_scheduled=0;

	nlsr_unlock();

	return 0;
}

void 
calculate_path(int **adj_matrix, long int *parent,long int *dist ,long int V, long int S)
{
	int i;
	long int v,u;
	//long int *dist=(long int *)malloc(V * sizeof(long int));
	long int *Q=(long int *)malloc(V * sizeof(long int));
	long int head=0;
	/* Initiate the Parent */
	for (i = 0 ; i < V; i++)
	{
		parent[i]=EMPTY_PARENT;
		dist[i]=INF_DISTANCE;
		Q[i]=i;
	} 

	if ( S != NO_MAPPING_NUM )
	{
		dist[S]=0;
		sort_queue_by_distance(Q,dist,head,V);

		while (head < V )
		{
			u=Q[head];
			if(dist[u] == INF_DISTANCE)
			{
				break;
			}

			for(v=0 ; v <V; v++)
			{
				if( adj_matrix[u][v] > 0 ) //
				{
					if ( is_not_explored(Q,v,head+1,V) )
					{
					
						if( dist[u] + adj_matrix[u][v] <  dist[v])
						{
							dist[v]=dist[u] + adj_matrix[u][v] ;
							parent[v]=u;
						}	

					}

				}

			}

			head++;
			sort_queue_by_distance(Q,dist,head,V);
		}
	}
	free(Q);	
}

void
print_all_path_from_source(long int *parent,long int source)
{
	int i, map_element;
	struct map_entry *me;

	struct hashtb_enumerator ee;
    	struct hashtb_enumerator *e = &ee;
    	
    	hashtb_start(nlsr->map, e);
	map_element=hashtb_n(nlsr->map);

	if ( source != NO_MAPPING_NUM)
	{
		for(i=0;i<map_element;i++)
		{
			me=e->data;
			if(me->mapping != source)
			{
				
				if ( nlsr->debugging )
				{
					print_path(parent,(long int)me->mapping);
					printf("\n");
				}
				if ( nlsr->detailed_logging )
				{
					print_path(parent,(long int)me->mapping);
					writeLogg(__FILE__,__FUNCTION__,__LINE__,"\n");
				}
				
			}
			hashtb_next(e);		
		}
	}
	hashtb_end(e);

}

void 
print_all_next_hop(long int *parent,long int source)
{
	int i, map_element;
	struct map_entry *me;

	struct hashtb_enumerator ee;
    	struct hashtb_enumerator *e = &ee;
    	
    	hashtb_start(nlsr->map, e);
	map_element=hashtb_n(nlsr->map);

	for(i=0;i<map_element;i++)
	{
		me=e->data;
		if(me->mapping != source)
		{
			if ( nlsr->debugging )
				printf("Dest: %d Next Hop: %ld\n",me->mapping,get_next_hop_from_calculation(parent,me->mapping,source));
			if ( nlsr->detailed_logging )
				writeLogg(__FILE__,__FUNCTION__,__LINE__,"Dest: %d Next Hop: %ld\n",me->mapping,get_next_hop_from_calculation(parent,me->mapping,source));
		}
		hashtb_next(e);		
	}

	hashtb_end(e);

}

long int
get_next_hop_from_calculation(long int *parent, long int dest,long int source)
{
	long int next_hop;
	while ( parent[dest] != EMPTY_PARENT )
	{
		next_hop=dest;
		dest=parent[dest];

	}

	if ( dest != source )
	{
		next_hop=NO_NEXT_HOP;	
	}
	
	return next_hop;
	
}

void
print_path(long int *parent, long int dest)
{
	if (parent[dest] != EMPTY_PARENT )
		print_path(parent,parent[dest]);

	if ( nlsr->debugging )
		printf(" %ld",dest);
}

int 
is_not_explored(long int *Q, long int u,long int start, long int element)
{
	int ret=0;
	long int i;
	for(i=start; i< element; i++)
	{
		if ( Q[i] == u )
		{
			ret=1;
			break;
		}
	}
	return ret;
}

void 
sort_queue_by_distance(long int *Q,long int *dist,long int start,long int element)
{
	long int i,j;
	long int temp_u;

	for ( i=start ; i < element ; i ++) 
	{
		for( j=i+1; j<element; j ++)
		{
			if (dist[Q[j]] < dist[Q[i]])
			{
				temp_u=Q[j];
				Q[j]=Q[i];
				Q[i]=temp_u;
			}
		}
	}
	
}

void 
sort_hyperbolic_route(double *dist_dest,double *dist_nbr, long int *faces,long int start,long int element)
{
	long int i,j;
	double temp_dist;
	long int temp_face;

	for ( i=start ; i < element ; i ++) 
	{
		for( j=i+1; j<element; j ++)
		{
			if (dist_dest[i] < dist_dest[j])
			{
				temp_dist=dist_dest[j];
				dist_dest[j]=dist_dest[i];
				dist_dest[i]=temp_dist;

				temp_dist=dist_nbr[j];
				dist_nbr[j]=dist_nbr[i];
				dist_nbr[i]=temp_dist;

				temp_face=faces[j];
				faces[j]=faces[i];
				faces[i]=temp_face;
			}
			if ( (dist_dest[i] == dist_dest[j]) && (dist_nbr[i] < dist_nbr[j]) )
			{
				temp_dist=dist_dest[j];
				dist_dest[j]=dist_dest[i];
				dist_dest[i]=temp_dist;

				temp_dist=dist_nbr[j];
				dist_nbr[j]=dist_nbr[i];
				dist_nbr[i]=temp_dist;

				temp_face=faces[j];
				faces[j]=faces[i];
				faces[i]=temp_face;
			}
		}
	}
	
}

void
print_map(void)
{
	int i, map_element;
	struct map_entry *me;

	struct hashtb_enumerator ee;
    	struct hashtb_enumerator *e = &ee;
    	
    	hashtb_start(nlsr->map, e);
	map_element=hashtb_n(nlsr->map);

	for(i=0;i<map_element;i++)
	{
		me=e->data;
		if ( nlsr->debugging )
			printf("Router: %s Mapping Number: %d \n",me->router,me->mapping);
		if ( nlsr->detailed_logging )
			writeLogg(__FILE__,__FUNCTION__,__LINE__,"Router: %s Mapping Number: %d \n",me->router,me->mapping);
		hashtb_next(e);		
	}

	hashtb_end(e);
}


void
assign_mapping_number(void)
{
	int i, map_element;
	struct map_entry *me;

	struct hashtb_enumerator ee;
    	struct hashtb_enumerator *e = &ee;
    	
    	hashtb_start(nlsr->map, e);
	map_element=hashtb_n(nlsr->map);

	for(i=0;i<map_element;i++)
	{
		me=e->data;
		me->mapping=i;
		add_rev_map_entry(i,me->router);
		hashtb_next(e);		
	}

	hashtb_end(e);
}

void 
make_map(void)
{
	

	int i, adj_lsdb_element;
	struct alsa *adj_lsa;

	struct hashtb_enumerator ee;
    	struct hashtb_enumerator *e = &ee;
    	
    	hashtb_start(nlsr->lsdb->adj_lsdb, e);
	adj_lsdb_element=hashtb_n(nlsr->lsdb->adj_lsdb);

	add_map_entry(nlsr->router_name);

	for(i=0;i<adj_lsdb_element;i++)
	{
		adj_lsa=e->data;
		add_adj_data_to_map(adj_lsa->header->orig_router->name,adj_lsa->body,adj_lsa->no_link);
		hashtb_next(e);		
	}

	hashtb_end(e);

}

void 
add_map_entry(char *router)
{

	struct map_entry *me;//=(struct map_entry*)malloc(sizeof(struct map_entry ));

	struct hashtb_enumerator ee;
    	struct hashtb_enumerator *e = &ee; 	
    	int res;

   	hashtb_start(nlsr->map, e);
    	res = hashtb_seek(e, router, strlen(router), 0);

	if(res == HT_NEW_ENTRY)
	{
		me=e->data;
		me->router=(char *)malloc(strlen(router)+1);
		memset(me->router,0,strlen(router)+1);
		memcpy(me->router,router,strlen(router));
		me->mapping=0;
	}

	hashtb_end(e);
	
}


void 
add_adj_data_to_map(char *orig_router, char *body, int no_link)
{
	add_map_entry(orig_router);
	
	int i=0;
	char *lsa_data=(char *)malloc(strlen(body)+1);
	memset(	lsa_data,0,strlen(body)+1);
	memcpy(lsa_data,body,strlen(body)+1);
	char *sep="|";
	char *rem;
	char *rtr_id;
	char *length;
	//char *face;
	char *metric;
	
	if(no_link >0 )
	{
		rtr_id=strtok_r(lsa_data,sep,&rem);
		length=strtok_r(NULL,sep,&rem);
		//face=strtok_r(NULL,sep,&rem);
		metric=strtok_r(NULL,sep,&rem);

		if ( !nlsr->debugging && nlsr->debugging)
		{
			printf("Metric: %s Length:%s\n",metric,length);
		}
		add_map_entry(rtr_id);

		for(i=1;i<no_link;i++)
		{
			rtr_id=strtok_r(NULL,sep,&rem);
			length=strtok_r(NULL,sep,&rem);
			//face=strtok_r(NULL,sep,&rem);
			metric=strtok_r(NULL,sep,&rem);
			if ( !nlsr->debugging && nlsr->debugging)
			{
				printf("Metric: %s Length:%s\n",metric,length);
			}

			add_map_entry(rtr_id);

		}
	}
	
	free(lsa_data);
}

int 
get_mapping_no(char *router)
{
	struct map_entry *me;

	struct hashtb_enumerator ee;
    	struct hashtb_enumerator *e = &ee; 	
    	int res;
	int ret;

	int n = hashtb_n(nlsr->map);

	if ( n < 1)
	{
		return NO_MAPPING_NUM;
	}

   	hashtb_start(nlsr->map, e);
    	res = hashtb_seek(e, router, strlen(router), 0);

	if( res == HT_OLD_ENTRY )
	{
		me=e->data;
		ret=me->mapping;
	}
	else if(res == HT_NEW_ENTRY)
	{
		hashtb_delete(e);
		ret=NO_MAPPING_NUM;
	}

	hashtb_end(e);	

	return ret;

}


void 
add_rev_map_entry(long int mapping_number, char *router)
{

	struct map_entry *me;//=(struct map_entry*)malloc(sizeof(struct map_entry ));

	struct hashtb_enumerator ee;
    	struct hashtb_enumerator *e = &ee; 	
    	int res;

   	hashtb_start(nlsr->rev_map, e);
    	res = hashtb_seek(e, &mapping_number, sizeof(mapping_number), 0);

	if(res == HT_NEW_ENTRY)
	{
		me=e->data;
		me->router=(char *)malloc(strlen(router)+1);
		memset(me->router,0,strlen(router)+1);
		memcpy(me->router,router,strlen(router));
		me->mapping=mapping_number;
	}

	hashtb_end(e);
	
}



char * 
get_router_from_rev_map(long int mapping_number)
{

	struct map_entry *me;

	struct hashtb_enumerator ee;
    	struct hashtb_enumerator *e = &ee; 	
    	int res;

   	hashtb_start(nlsr->rev_map, e);
    	res = hashtb_seek(e, &mapping_number, sizeof(mapping_number), 0);

	if(res == HT_OLD_ENTRY)
	{
		me=e->data;
		hashtb_end(e);
		return me->router;
	}
	else if(res == HT_NEW_ENTRY)
	{
		hashtb_delete(e);
		hashtb_end(e);
	}
	
	return NULL;
}

void
print_rev_map(void)
{
	int i, map_element;
	struct map_entry *me;

	struct hashtb_enumerator ee;
    	struct hashtb_enumerator *e = &ee;
    	
    	hashtb_start(nlsr->map, e);
	map_element=hashtb_n(nlsr->map);

	for(i=0;i<map_element;i++)
	{
		me=e->data;
		if ( nlsr->debugging )
			printf("Mapping Number: %d Router: %s  \n",me->mapping,me->router);
		if ( nlsr->detailed_logging )
			writeLogg(__FILE__,__FUNCTION__,__LINE__,"Mapping Number: %d Router: %s  \n",me->mapping,me->router);
		hashtb_next(e);		
	}

	hashtb_end(e);
}


void
assign_adj_matrix_for_lsa(struct alsa *adj_lsa, int **adj_matrix)
{
	int mapping_orig_router=get_mapping_no(adj_lsa->header->orig_router->name);
	int mapping_nbr_router;

	int i;
	char *lsa_data=(char *)malloc(strlen(adj_lsa->body)+1);
	memset(	lsa_data,0,strlen(adj_lsa->body)+1);
	memcpy(lsa_data,adj_lsa->body,strlen(adj_lsa->body)+1);
	char *sep="|";
	char *rem;
	char *rtr_id;
	char *length;
	//char *face;
	char *metric;
	
	if(adj_lsa->no_link >0 )
	{
		rtr_id=strtok_r(lsa_data,sep,&rem);
		length=strtok_r(NULL,sep,&rem);
		//face=strtok_r(NULL,sep,&rem);
		metric=strtok_r(NULL,sep,&rem);
	
		if ( !nlsr->debugging && nlsr->debugging)
		{
				printf("Metric: %s Length:%s\n",metric,length);
		}

		mapping_nbr_router=get_mapping_no(rtr_id);
		adj_matrix[mapping_orig_router][mapping_nbr_router]=atoi(metric);

		for(i=1;i<adj_lsa->no_link;i++)
		{
			rtr_id=strtok_r(NULL,sep,&rem);
			length=strtok_r(NULL,sep,&rem);
			//face=strtok_r(NULL,sep,&rem);
			metric=strtok_r(NULL,sep,&rem);

			if ( !nlsr->debugging && nlsr->debugging)
			{
				printf("Metric: %s Length:%s\n",metric,length);
			}

			mapping_nbr_router=get_mapping_no(rtr_id);
			adj_matrix[mapping_orig_router][mapping_nbr_router]=atoi(metric);

		}
	}
	
	free(lsa_data);
}

void 
make_adj_matrix(int **adj_matrix,int map_element)
{

	init_adj_matrix(adj_matrix,map_element);

	int i, adj_lsdb_element;
	struct alsa *adj_lsa;

	struct hashtb_enumerator ee;
    	struct hashtb_enumerator *e = &ee;
    	
    	hashtb_start(nlsr->lsdb->adj_lsdb, e);
	adj_lsdb_element=hashtb_n(nlsr->lsdb->adj_lsdb);

	for(i=0;i<adj_lsdb_element;i++)
	{
		adj_lsa=e->data;
		assign_adj_matrix_for_lsa(adj_lsa,adj_matrix);
		hashtb_next(e);		
	}

	hashtb_end(e);

}

void 
init_adj_matrix(int **adj_matrix,int map_element)
{
	int i, j;
	for(i=0;i<map_element;i++)
		for(j=0;j<map_element;j++)
			adj_matrix[i][j]=0;
}

void print_adj_matrix(int **adj_matrix, int map_element)
{
	int i, j;
	for(i=0;i<map_element;i++)
	{
		for(j=0;j<map_element;j++)
			printf("%d ",adj_matrix[i][j]);
		printf("\n");
	}
}


int 
get_no_link_from_adj_matrix(int **adj_matrix,long int V, long int S)
{
	int no_link=0;
	int i;

	for(i=0;i<V;i++)
	{	
		if ( adj_matrix[S][i] > 0 )
		{
			no_link++;
		}
	}
	return no_link;
}

void 
get_links_from_adj_matrix(int **adj_matrix, long int V ,long int *links, long int *link_costs,long int S)
{
	int i,j;
	j=0;
	for (i=0; i <V; i++)
	{
		if ( adj_matrix[S][i] > 0 )
		{
			links[j]=i;
			link_costs[j]=adj_matrix[S][i];
			j++;
		}
	}
}

void adjust_adj_matrix(int **adj_matrix, long int V, long int S, long int link,long int link_cost)
{
	int i;
	for ( i = 0; i < V; i++ )
	{
		if ( i == link )
		{
			adj_matrix[S][i]=link_cost;
		}
		else 
		{
			adj_matrix[S][i]=0;
		}
	}

}

int 
get_number_of_next_hop(char *dest_router)
{
	struct routing_table_entry *rte;

	struct hashtb_enumerator ee;
    	struct hashtb_enumerator *e = &ee; 	
    	int res,ret;

   	hashtb_start(nlsr->routing_table, e);
    	res = hashtb_seek(e, dest_router, strlen(dest_router), 0);

	if( res == HT_OLD_ENTRY )
	{
		rte=e->data;
		ret=hashtb_n(rte->face_list);
		//nhl=rte->face_list;
	}
	else if(res == HT_NEW_ENTRY)
	{
		hashtb_delete(e);
		ret=NO_NEXT_HOP;
	}

	hashtb_end(e);	

	return ret;
}


int 
get_next_hop(char *dest_router,int *faces, int *route_costs)
{
	struct routing_table_entry *rte;

	struct hashtb_enumerator ee;
    	struct hashtb_enumerator *e = &ee; 	
    	int res,ret;

   	hashtb_start(nlsr->routing_table, e);
    	res = hashtb_seek(e, dest_router, strlen(dest_router), 0);

	if( res == HT_OLD_ENTRY )
	{
		rte=e->data;
		ret=hashtb_n(rte->face_list);
		//nhl=rte->face_list;
		int j,face_list_element;
		struct face_list_entry *fle;

		struct hashtb_enumerator eef;
    		struct hashtb_enumerator *ef = &eef;
    	
    		hashtb_start(rte->face_list, ef);
		face_list_element=hashtb_n(rte->face_list);
		for(j=0;j<face_list_element;j++)
		{
			fle=ef->data;
			//printf(" 	Face: %d Route_Cost: %d \n",fle->next_hop_face,fle->route_cost);
			faces[j]=fle->next_hop_face;
			route_costs[j]=fle->route_cost;
			hashtb_next(ef);	
		}
		hashtb_end(ef);
		
	}
	else if(res == HT_NEW_ENTRY)
	{
		hashtb_delete(e);
		ret=NO_NEXT_HOP;
	}

	hashtb_end(e);	

	return ret;
}

void
add_next_hop_router(char *dest_router)
{
	if ( strcmp(dest_router,nlsr->router_name)== 0)
	{
		return ;
	}

	struct routing_table_entry *rte;//=(struct routing_table_entry *)malloc(sizeof(struct routing_table_entry));

	struct hashtb_enumerator ee;
    	struct hashtb_enumerator *e = &ee; 	
    	int res;

   	hashtb_start(nlsr->routing_table, e);
    	res = hashtb_seek(e, dest_router, strlen(dest_router), 0);

	if( res == HT_NEW_ENTRY )
	{
		rte=e->data;
		rte->dest_router=(char *)malloc(strlen(dest_router)+1);
		memset(rte->dest_router,0,strlen(dest_router)+1);
		memcpy(rte->dest_router,dest_router,strlen(dest_router));
		//rte->next_hop_face=NO_NEXT_HOP;
		struct hashtb_param param_fle = {0};
		rte->face_list=hashtb_create(sizeof(struct face_list_entry), &param_fle);

		add_npt_entry(dest_router, dest_router, 0, NULL, NULL);
	}
	hashtb_end(e);

}

void 
add_next_hop_from_lsa_adj_body(char *body, int no_link)
{

	int i=0;
	char *lsa_data=(char *)malloc(strlen(body)+1);
	memset(	lsa_data,0,strlen(body)+1);
	memcpy(lsa_data,body,strlen(body)+1);
	char *sep="|";
	char *rem;
	char *rtr_id;
	char *length;
	//char *face;
	char *metric;

	if(no_link >0 )
	{
		rtr_id=strtok_r(lsa_data,sep,&rem);
		length=strtok_r(NULL,sep,&rem);
		//face=strtok_r(NULL,sep,&rem);
		metric=strtok_r(NULL,sep,&rem);
		if ( !nlsr->debugging && nlsr->debugging)
		{
			printf("Metric: %s Length:%s\n",metric,length);
		}


		add_next_hop_router(rtr_id);

		for(i=1;i<no_link;i++)
		{
			rtr_id=strtok_r(NULL,sep,&rem);
			length=strtok_r(NULL,sep,&rem);
			//face=strtok_r(NULL,sep,&rem);
			metric=strtok_r(NULL,sep,&rem);
			if ( !nlsr->debugging && nlsr->debugging)
			{
				printf("Metric: %s Length:%s\n",metric,length);
			}

			add_next_hop_router(rtr_id);
	
		}
	}

	free(lsa_data);


}

void 
update_routing_table(char * dest_router,int next_hop_face, int route_cost)
{
	if ( nlsr->debugging )
	{
		printf("update_routing_table called \n");
		printf("Dest Router: %s Next Hop face: %d Route Cost: %d \n",dest_router,next_hop_face,route_cost);
	}

	int res,res1;
	struct routing_table_entry *rte;

	struct hashtb_enumerator ee;
    	struct hashtb_enumerator *e = &ee;
    	
    	hashtb_start(nlsr->routing_table, e);
	res = hashtb_seek(e, dest_router, strlen(dest_router), 0);

	if( res == HT_OLD_ENTRY )
	{
		rte=e->data;

		struct hashtb_enumerator eef;
    		struct hashtb_enumerator *ef = &eef;
    	
    		hashtb_start(rte->face_list, ef);
		res1 = hashtb_seek(ef, &next_hop_face, sizeof(next_hop_face), 0);	
		if( res1 == HT_NEW_ENTRY)
		{
			struct face_list_entry *fle;
			fle=ef->data;
			fle->next_hop_face=next_hop_face;
			fle->route_cost=route_cost;						
		}
		else if ( res1 == HT_OLD_ENTRY )
		{
			struct face_list_entry *fle;
			fle=ef->data;
			fle->route_cost=route_cost;
		}
		hashtb_end(ef);
	}
	else if ( res == HT_NEW_ENTRY )
	{
		hashtb_delete(e);
	}
	
	hashtb_end(e);
	
}

void 
print_routing_table(void)
{
	if ( nlsr->debugging )
		printf("print_routing_table called\n");
	if ( nlsr->detailed_logging )
		writeLogg(__FILE__,__FUNCTION__,__LINE__,"print_routing_table called\n");
	int i,j, rt_element,face_list_element;
	
	struct routing_table_entry *rte;

	struct hashtb_enumerator ee;
    	struct hashtb_enumerator *e = &ee;
    	
    	hashtb_start(nlsr->routing_table, e);
	rt_element=hashtb_n(nlsr->routing_table);

	for(i=0;i<rt_element;i++)
	{
		if ( nlsr->debugging )
			printf("----------Routing Table Entry %d------------------\n",i+1);
		if ( nlsr->detailed_logging )
			writeLogg(__FILE__,__FUNCTION__,__LINE__,"----------Routing Table Entry %d------------------\n",i+1);
		
		rte=e->data;

		if ( nlsr->debugging )
			printf(" Destination Router: %s \n",rte->dest_router);
		if ( nlsr->detailed_logging )
			writeLogg(__FILE__,__FUNCTION__,__LINE__," Destination Router: %s \n",rte->dest_router);

		
		//rte->next_hop_face == NO_NEXT_HOP ? printf(" Next Hop Face: NO_NEXT_HOP \n") : printf(" Next Hop Face: %d \n", rte->next_hop_face);

		struct face_list_entry *fle;

		struct hashtb_enumerator eef;
    		struct hashtb_enumerator *ef = &eef;
    	
    		hashtb_start(rte->face_list, ef);
		face_list_element=hashtb_n(rte->face_list);
		if ( face_list_element <= 0 )
		{
			if ( nlsr->debugging )
				printf(" 	Face: No Face \n");
			if ( nlsr->detailed_logging )
				writeLogg(__FILE__,__FUNCTION__,__LINE__," 	Face: No Face \n");
		}
		else
		{
			for(j=0;j<face_list_element;j++)
			{
				fle=ef->data;
				if ( nlsr->debugging )
					printf(" 	Face: %d Route_Cost: %f \n",fle->next_hop_face,fle->route_cost);
				if ( nlsr->detailed_logging )
					writeLogg(__FILE__,__FUNCTION__,__LINE__," 	Face: %d Route_Cost: %d \n",fle->next_hop_face,fle->route_cost);
				hashtb_next(ef);	
			}
		}
		hashtb_end(ef);

		hashtb_next(e);		
	}

	hashtb_end(e);

	if ( nlsr->debugging )
		printf("\n");
	if ( nlsr->detailed_logging )
		writeLogg(__FILE__,__FUNCTION__,__LINE__,"\n");
}

/*
int
delete_empty_rte(struct ccn_schedule *sched, void *clienth, struct ccn_scheduled_event *ev, int flags)
{
	if ( nlsr->debugging )
	{
		printf("delete_empty_rte called\n");
		printf("Router: %s \n",(char *)ev->evdata);
	}
	if ( nlsr->detailed_logging )
	{
		writeLogg(__FILE__,__FUNCTION__,__LINE__,"delete_empty_rte called\n");
		writeLogg(__FILE__,__FUNCTION__,__LINE__,"Router: %s \n",(char *)ev->evdata);
	}
	
	if(flags == CCN_SCHEDULE_CANCEL)
	{
 	 	return -1;
	}

	nlsr_lock();
	int res;
	struct hashtb_enumerator ee;
    	struct hashtb_enumerator *e = &ee;
    	
    	hashtb_start(nlsr->routing_table, e);
	res = hashtb_seek(e, (char *)ev->evdata, strlen((char *)ev->evdata), 0);
	
	if ( res == HT_OLD_ENTRY )
	{
		hashtb_delete(e);
	}
	else if ( res == HT_NEW_ENTRY )
	{
		hashtb_delete(e);
	}

	print_routing_table();

	nlsr_unlock();
	
	return 0;
}
*/

void 
clear_old_routing_table(void)
{
	if ( nlsr->debugging )
		printf("clear_old_routing_table called\n");
	if ( nlsr->detailed_logging )
		writeLogg(__FILE__,__FUNCTION__,__LINE__,"clear_old_routing_table called\n");
	int i,rt_element;
	
	struct routing_table_entry *rte;

	struct hashtb_enumerator ee;
    	struct hashtb_enumerator *e = &ee;
    	
    	hashtb_start(nlsr->routing_table, e);
	rt_element=hashtb_n(nlsr->routing_table);

	for(i=0;i<rt_element;i++)
	{
		rte=e->data;
		hashtb_destroy(&rte->face_list);
		struct hashtb_param param_fle = {0};
		rte->face_list=hashtb_create(sizeof(struct face_list_entry), &param_fle);

		hashtb_next(e);		
	}

	hashtb_end(e);	
}


void 
do_old_routing_table_updates(void)
{
	if ( nlsr->debugging )
		printf("do_old_routing_table_updates called\n");
	if ( nlsr->detailed_logging )
		writeLogg(__FILE__,__FUNCTION__,__LINE__,"do_old_routing_table_updates called\n");
	
	int i, rt_element;
	int mapping_no;	

	struct routing_table_entry *rte;

	struct hashtb_enumerator ee;
    	struct hashtb_enumerator *e = &ee;
    	
    	hashtb_start(nlsr->routing_table, e);
	rt_element=hashtb_n(nlsr->routing_table);

	for(i=0;i<rt_element;i++)
	{
		rte=e->data;
		mapping_no=get_mapping_no(rte->dest_router);
		if ( mapping_no == NO_MAPPING_NUM)
		{		
			delete_orig_router_from_npt(rte->dest_router);
			/*
			char *router=(char *)malloc(strlen(rte->dest_router)+1);
			memset(router,0,strlen(rte->dest_router)+1);
			memcpy(router,rte->dest_router,strlen(rte->dest_router)+1);
			nlsr->event = ccn_schedule_event(nlsr->sched, 1, &delete_empty_rte, (void *)router , 0);
			*/
			destroy_routing_table_entry_comp(rte);
			hashtb_delete(e);
			i++;
		}
		else
		{
			hashtb_next(e);
		}		
	}

	hashtb_end(e);	
}

void 
update_routing_table_with_new_hyperbolic_route(long int dest_router_rev_map_index, long int face, double nbr_to_dest_dist)
{
	if ( nlsr->debugging )
		printf("update_routing_table_with_new_hyperbolic_route called\n");
	if ( nlsr->detailed_logging )
		writeLogg(__FILE__,__FUNCTION__,__LINE__,"update_routing_table_with_new_hyperbolic_route called\n");
	
	char *orig_router=get_router_from_rev_map(dest_router_rev_map_index);

	if (face != NO_NEXT_HOP && face != NO_FACE )
	{
		update_routing_table(orig_router,face,nbr_to_dest_dist);
		if ( nlsr->debugging )
			printf ("Orig_router: %s Next Hop Face: %ld \n",orig_router,face);
		if ( nlsr->detailed_logging )
			writeLogg(__FILE__,__FUNCTION__,__LINE__,"Orig_router: %s Next Hop Face: %ld \n",orig_router,face);
					
	}
	

}


void 
update_routing_table_with_new_route(long int *parent, long int *dist,long int source)
{
	if ( nlsr->debugging )
		printf("update_routing_table_with_new_route called\n");
	if ( nlsr->detailed_logging )
		writeLogg(__FILE__,__FUNCTION__,__LINE__,"update_routing_table_with_new_route called\n");
	int i, map_element;
	struct map_entry *me;

	struct hashtb_enumerator ee;
    	struct hashtb_enumerator *e = &ee;
    	
    	hashtb_start(nlsr->rev_map, e);
	map_element=hashtb_n(nlsr->rev_map);

	for(i=0;i<map_element;i++)
	{
		me=e->data;
		if(me->mapping != source)
		{
			
			char *orig_router=get_router_from_rev_map(me->mapping);
			if (orig_router != NULL )
			{
				int next_hop_router_num=get_next_hop_from_calculation(parent,me->mapping,source);
				if ( next_hop_router_num == NO_NEXT_HOP )
				{
					if ( nlsr->debugging )
						printf ("Orig_router: %s Next Hop Face: %d \n",orig_router,NO_FACE);
					if ( nlsr->detailed_logging )
						writeLogg(__FILE__,__FUNCTION__,__LINE__,"Orig_router: %s Next Hop Face: %d \n",orig_router,NO_FACE);
				}
				else 
				{
					char *next_hop_router=get_router_from_rev_map(next_hop_router_num);
					int next_hop_face=get_next_hop_face_from_adl(next_hop_router);
					update_routing_table(orig_router,next_hop_face,dist[me->mapping]);
					if ( nlsr->debugging )
						printf ("Orig_router: %s Next Hop Face: %d \n",orig_router,next_hop_face);
					if ( nlsr->detailed_logging )
						writeLogg(__FILE__,__FUNCTION__,__LINE__,"Orig_router: %s Next Hop Face: %d \n",orig_router,next_hop_face);
					

				}
			}
		}
		hashtb_next(e);		
	}

	hashtb_end(e);
}

int 
does_face_exist_for_router(char *dest_router, int face_id)
{
	if (nlsr->debugging)
	{
		printf("does_face_exist_for_router called\n");
		printf("Dest Router: %s and Face id: %d \n",dest_router, face_id);
	}

	int ret=0;

	int res;
	struct routing_table_entry *rte;

	struct hashtb_enumerator ee;
    	struct hashtb_enumerator *e = &ee;
    	
    	hashtb_start(nlsr->routing_table, e);
	res = hashtb_seek(e, dest_router, strlen(dest_router), 0);

	if( res == HT_OLD_ENTRY )
	{
		rte=e->data;
		unsigned *v;
		v = hashtb_lookup(rte->face_list, &face_id, sizeof(face_id));
		if (v != NULL)
			ret = 1;
	}
	else
	{
		hashtb_delete(e);
	} 
	
	hashtb_end(e);

	return ret;
}

double 
get_hyperbolic_distance(long int source, long int dest)
{
	double distance;
	char *src_router=get_router_from_rev_map(source);
	char *dest_router=get_router_from_rev_map(dest);

	double src_r=get_hyperbolic_r(src_router);
	double src_theta=get_hyperbolic_r(src_router);	

	double dest_r=get_hyperbolic_r(dest_router);
	double dest_theta=get_hyperbolic_r(dest_router);
	if ( src_r != -1 && dest_r != -1 )
	{
		distance=acosh(cosh(src_r)*cosh(dest_r)-sinh(src_r)*sinh(dest_r)*cos(src_theta-dest_theta));
	}
	else 
	{
		distance= -1.0;
	}
	
	return distance;
}

void
destroy_routing_table_entry_comp(struct routing_table_entry *rte)
{

	free(rte->dest_router);
	hashtb_destroy(&rte->face_list);	
		
}

void
destroy_routing_table_entry(struct routing_table_entry *rte)
{

	destroy_routing_table_entry_comp(rte);
	free(rte);
}

void
destroy_routing_table(void)
{
	int i, rt_element;
	struct hashtb_enumerator ee;
	struct hashtb_enumerator *e = &ee;

	struct routing_table_entry *rte;
	hashtb_start(nlsr->routing_table, e);
	rt_element=hashtb_n(nlsr->routing_table);
	for(i=0;i<rt_element;i++)
	{
		rte=e->data;
		free(rte->dest_router);
		hashtb_destroy(&rte->face_list);	
		hashtb_next(e);		
	}	
	hashtb_end(e);
	hashtb_destroy(&nlsr->routing_table);

}

void
destroy_map(void)
{

	int i, map_element;
	struct map_entry *me;

	struct hashtb_enumerator ee;
    	struct hashtb_enumerator *e = &ee;
    	
    	hashtb_start(nlsr->map, e);
	map_element=hashtb_n(nlsr->map);

	for(i=0;i<map_element;i++)
	{
		me=e->data;
		free(me->router);
		hashtb_next(e);		
	}

	hashtb_end(e);

}

void
destroy_rev_map(void)
{
	int i, map_element;
	struct map_entry *me;

	struct hashtb_enumerator ee;
    	struct hashtb_enumerator *e = &ee;
    	
    	hashtb_start(nlsr->rev_map, e);
	map_element=hashtb_n(nlsr->rev_map);

	for(i=0;i<map_element;i++)
	{
		me=e->data;
		free(me->router);
		hashtb_next(e);		
	}

	hashtb_end(e);
}
