/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include "abti.h"

static ABT_Task_id ABTI_Task_get_new_id();


int ABT_Task_create(void (*task_func)(void *), void *arg, ABT_Task *newtask)
{
    int abt_errno = ABT_SUCCESS;
    ABTI_Task *p_newtask;
    ABT_Task h_newtask;

    ABTI_Stream_pool *p_streams = gp_ABT->p_streams;
    if (p_streams->num_active == 0 &&
        ABTI_Pool_get_size(p_streams->pool) > 0) {
        abt_errno = ABTI_Stream_start_any();
        if (abt_errno != ABT_SUCCESS) {
            HANDLE_ERROR("ABTI_Stream_start_any");
            goto fn_fail;
        }
    }

    p_newtask = (ABTI_Task *)ABTU_Malloc(sizeof(ABTI_Task));
    if (!p_newtask) {
        HANDLE_ERROR("ABTU_Malloc");
        if (newtask) *newtask = NULL;
        abt_errno = ABT_ERR_MEM;
        goto fn_fail;
    }

    p_newtask->p_stream = NULL;
    p_newtask->id       = ABTI_Task_get_new_id();
    p_newtask->p_name   = NULL;
    p_newtask->refcount = (newtask != NULL) ? 1 : 0;
    p_newtask->state    = ABT_TASK_STATE_CREATED;
    p_newtask->f_task   = task_func;
    p_newtask->p_arg    = arg;

    /* Create a wrapper work unit */
    h_newtask = ABTI_Task_get_handle(p_newtask);
    p_newtask->unit = ABTI_Unit_create_from_task(h_newtask);

    /* Add this task to the global task pool */
    ABTI_Task_pool *p_tasks = gp_ABT->p_tasks;
    ABTD_ES_lock(&p_tasks->lock);
    ABTI_Pool_push(p_tasks->pool, p_newtask->unit);
    ABTD_ES_unlock(&p_tasks->lock);

    /* Return value */
    if (newtask) *newtask = h_newtask;

  fn_exit:
    return abt_errno;

  fn_fail:
    goto fn_exit;
}

int ABT_Task_create_for_stream(const ABT_Stream stream,
                    void (*task_func)(void *), void *arg, ABT_Task *newtask)
{
    int abt_errno = ABT_SUCCESS;
    ABTI_Stream *p_stream;
    ABTI_Scheduler *p_sched;
    ABTI_Task *p_newtask;
    ABT_Task h_newtask;

    p_stream = ABTI_Stream_get_ptr(stream);
    if (p_stream->state == ABT_STREAM_STATE_CREATED) {
        abt_errno = ABTI_Stream_start(p_stream);
        if (abt_errno != ABT_SUCCESS) {
            HANDLE_ERROR("ABTI_Stream_start");
            goto fn_fail;
        }
    }

    p_newtask = (ABTI_Task *)ABTU_Malloc(sizeof(ABTI_Task));
    if (!p_newtask) {
        HANDLE_ERROR("ABTU_Malloc");
        if (newtask) *newtask = NULL;
        abt_errno = ABT_ERR_MEM;
        goto fn_fail;
    }

    p_newtask->p_stream = p_stream;
    p_newtask->id       = ABTI_Task_get_new_id();
    p_newtask->p_name   = NULL;
    p_newtask->refcount = (newtask != NULL) ? 1 : 0;
    p_newtask->state    = ABT_TASK_STATE_CREATED;
    p_newtask->f_task   = task_func;
    p_newtask->p_arg    = arg;

    /* Create a wrapper work unit */
    p_sched = p_stream->p_sched;
    h_newtask = ABTI_Task_get_handle(p_newtask);
    p_newtask->unit = p_sched->u_create_from_task(h_newtask);

    /* Add this task to the scheduler's pool */
    ABTD_ES_lock(&p_stream->lock);
    p_sched->p_push(p_sched->pool, p_newtask->unit);
    ABTD_ES_unlock(&p_stream->lock);

    /* Return value */
    if (newtask) *newtask = h_newtask;

  fn_exit:
    return abt_errno;

  fn_fail:
    goto fn_exit;
}

int ABT_Task_free(ABT_Task task)
{
    int abt_errno = ABT_SUCCESS;
    ABTI_Task_pool *p_tasks = gp_ABT->p_tasks;
    ABTI_Task *p_task = ABTI_Task_get_ptr(task);

    if (p_task->state == ABT_TASK_STATE_COMPLETED ||
        p_task->state == ABT_TASK_STATE_CANCELED) {
        /* The task has been completed */
        if (p_task->refcount > 0) {
            ABTI_Pool_remove(p_tasks->deads, p_task->unit);
            ABTI_Unit_free(p_task->unit);
        } else {
            ABTI_Unit_free(p_task->unit);
        }
    } else if (p_task->state == ABT_TASK_STATE_CREATED) {
        ABTI_Pool_remove(p_tasks->pool, p_task->unit);
        ABTI_Unit_free(p_task->unit);
    } else if (p_task->state == ABT_TASK_STATE_DELAYED) {
        ABTI_Stream *p_stream = p_task->p_stream;
        ABTI_Scheduler *p_sched = p_stream->p_sched;
        p_sched->p_remove(p_sched->pool, p_task->unit);
        p_sched->u_free(p_task->unit);
    } else {
        HANDLE_ERROR("Cannot free the task");
        abt_errno = ABT_ERR_TASK;
        goto fn_fail;
    }

    /* Free the ABTI_Task structure */
    if (p_task->p_name) ABTU_Free(p_task->p_name);
    ABTU_Free(p_task);

  fn_exit:
    return abt_errno;

  fn_fail:
    goto fn_exit;
}

int ABT_Task_equal(ABT_Task task1, ABT_Task task2)
{
    ABTI_Task *p_task1 = ABTI_Task_get_ptr(task1);
    ABTI_Task *p_task2 = ABTI_Task_get_ptr(task2);
    return p_task1 == p_task2;
}

ABT_Task_state ABT_Task_get_state(ABT_Task task)
{
    ABTI_Task *p_task = ABTI_Task_get_ptr(task);
    return p_task->state;
}

int ABT_Task_set_name(ABT_Task task, const char *name)
{
    int abt_errno = ABT_SUCCESS;
    ABTI_Task *p_task = ABTI_Task_get_ptr(task);

    size_t len = strlen(name);
    if (p_task->p_name) ABTU_Free(p_task->p_name);
    p_task->p_name = (char *)ABTU_Malloc(len + 1);
    if (!p_task->p_name) {
        HANDLE_ERROR("ABTU_Malloc");
        abt_errno = ABT_ERR_MEM;
        goto fn_fail;
    }
    ABTU_Strcpy(p_task->p_name, name);

  fn_exit:
    return abt_errno;

  fn_fail:
    goto fn_exit;
}

int ABT_Task_get_name(ABT_Task task, char *name, size_t len)
{
    int abt_errno = ABT_SUCCESS;
    ABTI_Task *p_task = ABTI_Task_get_ptr(task);

    size_t name_len = strlen(p_task->p_name);
    if (name_len >= len) {
        ABTU_Strncpy(name, p_task->p_name, len - 1);
        name[len - 1] = '\0';
    } else {
        ABTU_Strncpy(name, p_task->p_name, name_len);
        name[name_len] = '\0';
    }

    return abt_errno;
}


/* Internal non-static functions */
int ABTI_Task_init(ABTI_Task_pool *p_tasks)
{
    int abt_errno = ABT_SUCCESS;

    /* Create a global task pool */
    ABTI_Pool *p_pool;
    if (ABTI_Pool_create(&p_pool) != ABT_SUCCESS) {
        HANDLE_ERROR("ABTI_Pool_create");
        abt_errno = ABT_ERR_TASK;
        goto fn_fail;
    }
    p_tasks->pool = ABTI_Pool_get_handle(p_pool);

    /* Create a pool where completed tasks are preserved */
    ABTI_Pool *p_deads;
    if (ABTI_Pool_create(&p_deads) != ABT_SUCCESS) {
        HANDLE_ERROR("ABTI_Pool_create");
        abt_errno = ABT_ERR_TASK;
        goto fn_fail;
    }
    p_tasks->deads = ABTI_Pool_get_handle(p_deads);

    /* Initialize the lock variable for the global task pool */
    if (ABTD_ES_lock_create(&p_tasks->lock, NULL) != ABTD_ES_SUCCESS) {
        HANDLE_ERROR("ABTD_ES_lock_create");
        abt_errno = ABT_ERR_TASK;
        goto fn_fail;
    }

  fn_exit:
    return abt_errno;

  fn_fail:
    goto fn_exit;
}

int ABTI_Task_finalize(ABTI_Task_pool *p_tasks)
{
    int abt_errno = ABT_SUCCESS;

    /* Release all task objects if they exist in the pool */
    ABT_Pool pool = p_tasks->pool;
    ABTI_Pool *p_pool = ABTI_Pool_get_ptr(pool);
    while (p_pool->num_units > 0) {
        ABT_Unit unit = ABTI_Pool_pop(pool);
        ABT_Task task = ABTI_Unit_get_task(unit);
        assert(task != ABT_TASK_NULL);

        /* Free the ABTI_Task structure */
        ABTI_Task *p_task = ABTI_Task_get_ptr(task);
        if (p_task->p_name) ABTU_Free(p_task->p_name);
        ABTU_Free(p_task);

        /* Free the associated work unit */
        ABTI_Unit_free(unit);
    }
    abt_errno = ABTI_Pool_free(pool);
    if (abt_errno  != ABT_SUCCESS) {
        HANDLE_ERROR("ABTI_Pool_free");
        goto fn_fail;
    }

    /* Release all remaining tasks in the deads pool */
    ABT_Pool deads = p_tasks->deads;
    ABTI_Pool *p_deads = ABTI_Pool_get_ptr(deads);
    while (p_deads->num_units > 0) {
        ABT_Unit unit = ABTI_Pool_pop(deads);
        ABT_Task task = ABTI_Unit_get_task(unit);
        assert(task != ABT_TASK_NULL);

        /* Free the ABTI_Task structure */
        ABTI_Task *p_task = ABTI_Task_get_ptr(task);
        if (p_task->p_name) ABTU_Free(p_task->p_name);
        ABTU_Free(p_task);

        /* Release the associated work unit */
        ABTI_Unit_free(unit);
    }
    abt_errno = ABTI_Pool_free(deads);
    if (abt_errno != ABT_SUCCESS) {
        HANDLE_ERROR("ABTI_Pool_free");
        goto fn_fail;
    }

    /* Destroy the lock variable */
    ABTD_ES_lock_free(&p_tasks->lock);

  fn_exit:
    return abt_errno;

  fn_fail:
    goto fn_exit;
}

int  ABTI_Task_execute()
{
    int ret = ABT_SUCCESS;

    ABTI_Task_pool *p_tasks = gp_ABT->p_tasks;
    ABT_Pool pool = p_tasks->pool;
    ABT_Unit unit;

    ABTD_ES_lock(&p_tasks->lock);
    if (ABTI_Pool_get_size(pool) == 0) {
        ret = ABT_INVALID;
        ABTD_ES_unlock(&p_tasks->lock);
        goto fn_exit;
    } else {
        unit = ABTI_Pool_pop(pool);
    }
    ABTD_ES_unlock(&p_tasks->lock);

    /* Exeucute the task */
    ABT_Task task = ABTI_Unit_get_task(unit);
    ABTI_Task *p_task = ABTI_Task_get_ptr(task);
    p_task->state = ABT_TASK_STATE_RUNNING;
    p_task->f_task(p_task->p_arg);
    p_task->state = ABT_TASK_STATE_COMPLETED;

    if (p_task->refcount == 0) {
        ABT_Task_free(task);
    } else {
        ABTI_Task_keep(p_task);
    }

  fn_exit:
    return ret;
}

void ABTI_Task_keep(ABTI_Task *p_task)
{
    ABTI_Task_pool *p_tasks = gp_ABT->p_tasks;
    ABTI_Pool_push(p_tasks->deads, p_task->unit);
}


/* Internal static functions */
static ABT_Task_id g_task_id = 0;
static ABT_Task_id ABTI_Task_get_new_id()
{
    ABT_Task_id new_id;

    ABTI_Task_pool *p_tasks = gp_ABT->p_tasks;
    ABTD_ES_lock(&p_tasks->lock);
    new_id = g_task_id++;
    ABTD_ES_unlock(&p_tasks->lock);

    return new_id;
}

