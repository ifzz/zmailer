/*
 * Note: this file originally auto-generated by mib2c using
 *        : mib2c.iterate.conf,v 5.9 2003/06/04 00:14:41 hardaker Exp $
 */
#ifndef MTA_H
#define MTA_H

/*
 * function declarations 
 */
void            init_mta(void);
void            initialize_table_mtaGroupTable(void);
Netsnmp_Node_Handler mtaGroupTable_handler;

Netsnmp_First_Data_Point mtaGroupTable_get_first_data_point;
Netsnmp_Next_Data_Point mtaGroupTable_get_next_data_point;
void            initialize_table_mtaGroupAssociationTable(void);
Netsnmp_Node_Handler mtaGroupAssociationTable_handler;

Netsnmp_First_Data_Point mtaGroupAssociationTable_get_first_data_point;
Netsnmp_Next_Data_Point mtaGroupAssociationTable_get_next_data_point;
void            initialize_table_mtaTable(void);
Netsnmp_Node_Handler mtaTable_handler;

Netsnmp_First_Data_Point mtaTable_get_first_data_point;
Netsnmp_Next_Data_Point mtaTable_get_next_data_point;
void            initialize_table_mtaGroupErrorTable(void);
Netsnmp_Node_Handler mtaGroupErrorTable_handler;

Netsnmp_First_Data_Point mtaGroupErrorTable_get_first_data_point;
Netsnmp_Next_Data_Point mtaGroupErrorTable_get_next_data_point;

/*
 * column number definitions for table mtaGroupTable 
 */
#include "mta_columns.h"

/*
 * enum definions 
 */
#include "mta_enums.h"

#endif                          /* MTA_H */
