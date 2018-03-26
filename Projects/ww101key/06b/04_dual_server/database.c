#include "wiced.h"
#include "database.h"

////////////////////// Database  /////////////

/// In order to save space and make the program simpler I have a linked list of all
// of the writes to the database.
//
// Read:
// When I get a read from the remote server I just look  through the linked list to
// find if that has deviceId/regId combination has ever been written and either return it
// or a f
//
// Write:
// When I get a write I look through the linked list to find that deviceId/regId combination
// if yes then overwrite.. otherwise add it to the list
//
#define dbMax (400)
uint32_t dbGetMax()
{
    return dbMax ;
}


linked_list_t db;

wiced_mutex_t dbMutex;

// initialize the database
void dbStart(void)
{
    linked_list_init(&db);
    wiced_rtos_init_mutex(&dbMutex);
}

// This function is  used by the linked_list library to compare two dbEntries for equality
// e.g when searching for a dbEntry
static wiced_bool_t dbCompare( linked_list_node_t* node_to_compare, void* user_data )
{

    // if the device ID and regId are the same
    dbEntry_t *p1 = (dbEntry_t *)node_to_compare->data;
    dbEntry_t *p2 = (dbEntry_t *)user_data;

    if(p1->deviceId == p2->deviceId && p1->regId == p2->regId)
        return WICED_TRUE;
    return WICED_FALSE;
}

// dbFind:
// Search the database for specific deviceId/regId combination
dbEntry_t *dbFind(dbEntry_t *find)
{
    linked_list_node_t *found;
    dbEntry_t *rval = NULL;

    wiced_rtos_lock_mutex(&dbMutex);
    if(linked_list_find_node( &db, dbCompare, (void*) find, &found ) == WICED_SUCCESS)
        rval =  found->data;
    wiced_rtos_unlock_mutex(&dbMutex);

    return rval;
}

// dbSetValue
// searches the database and newValue is not found then it inserts it or
// overwrite the value
//
void dbSetValue(dbEntry_t *newValue)
{
    dbEntry_t *found = dbFind(newValue);
    if(found) // if it is already in the database
    {
        found->value = newValue->value;
    }
    else // add it to the linked list
    {
        wiced_rtos_lock_mutex(&dbMutex);

        linked_list_node_t *newNode = (linked_list_node_t *)malloc(sizeof(linked_list_node_t));
        newNode->data = newValue;
        linked_list_insert_node_at_front( &db, newNode );
        wiced_rtos_unlock_mutex(&dbMutex);

    }
}

uint32_t dbGetCount()
{
    uint32_t count;
    linked_list_get_count(&db,&count);
    return count;

}


//////////////// End of Database ////////////////
