#include <context.h>
#include <memory.h>
#include <lib.h>
#include <entry.h>
#include <file.h>
#include <tracer.h>

#define MAX_OPEN_FILES 16
///////////////////////////////////////////////////////////////////////////
//// 		Start of Trace buffer functionality 		      /////
///////////////////////////////////////////////////////////////////////////

int is_valid_mem_range(unsigned long buff, u32 count, int access_bit)
{
    struct exec_context *ctx = get_current_ctx();

    // Invalid Arguments passed
    if (!buff || count < 0)
    {
        return -EBADMEM;
    }

    // Checking range where buff lies
    if (buff >= ctx->mms[MM_SEG_CODE].start && buff + (unsigned long)count < ctx->mms[MM_SEG_CODE].next_free)
    {
        if ((ctx->mms[MM_SEG_CODE].access_flags & access_bit) == access_bit)
        {
            return 0;
        }
        else
        {
            return -EBADMEM;
        }
    }
    if (buff >= ctx->mms[MM_SEG_RODATA].start && buff + (unsigned long)count < ctx->mms[MM_SEG_RODATA].next_free)
    {
        if ((ctx->mms[MM_SEG_RODATA].access_flags & access_bit) == access_bit)
        {
            return 0;
        }
        else
        {
            return -EBADMEM;
        }
    }
    if (buff >= ctx->mms[MM_SEG_DATA].start && buff + (unsigned long)count < ctx->mms[MM_SEG_DATA].next_free)
    {
        if ((ctx->mms[MM_SEG_DATA].access_flags & access_bit) == access_bit)
        {
            return 0;
        }
        else
        {
            return -EBADMEM;
        }
    }
    if (buff >= ctx->mms[MM_SEG_STACK].start && buff + (unsigned long)count < ctx->mms[MM_SEG_STACK].end)
    {
        if ((ctx->mms[MM_SEG_STACK].access_flags & access_bit) == access_bit)
        {
            return 0;
        }
        else
        {
            return -EBADMEM;
        }
    }

    while (ctx->vm_area->vm_next != NULL)
    {
        if (buff >= ctx->vm_area->vm_start && buff + (unsigned long)count < ctx->vm_area->vm_end)
        {
            if ((ctx->vm_area->access_flags & access_bit) == access_bit)
            {
                return 0;
            }
        }
        ctx->vm_area = ctx->vm_area->vm_next;
    }
    if (buff >= ctx->vm_area->vm_start && buff + (unsigned long)count < ctx->vm_area->vm_end)
    {
        if ((ctx->vm_area->access_flags & access_bit) == access_bit)
        {
            return 0;
        }
    }
    return -EBADMEM;
}

long trace_buffer_close(struct file *filep)
{
    if (!filep || filep->type != TRACE_BUFFER)
    {
        return -EINVAL;
    }

    os_page_free(USER_REG, filep->trace_buffer->trace_mem);
    os_page_free(USER_REG, filep->trace_buffer);
    os_free(filep->fops, sizeof(struct fileops));
    os_free(filep, sizeof(struct file));

    // printk("trace_buffer_close called.\n");
    return 0;
}

int trace_buffer_read(struct file *filep, char *buff, u32 count)
{
    // Check if the filep is valid and is associated with a trace buffer
    if (!filep || filep->type != TRACE_BUFFER)
    {
        return -EINVAL;
    }

    if (is_valid_mem_range((unsigned long)buff, count, 2) != 0)
    {
        // The buffer is not valid for write access
        return -EBADMEM;
    }

    u32 available_space = 0;

    // checking the available space for all the three different possible cases
    if (filep->trace_buffer->offp_read < filep->trace_buffer->offp_write)
    {
        available_space = filep->trace_buffer->offp_write - filep->trace_buffer->offp_read;
    }
    else if (filep->trace_buffer->offp_read > filep->trace_buffer->offp_write)
    {
        available_space = 4096 - filep->trace_buffer->offp_read + filep->trace_buffer->offp_write;
    }
    else
    {
        available_space = 4096 - filep->trace_buffer->offp_read + filep->trace_buffer->offp_write;
        if (filep->trace_buffer->check_fullness == 0)
        {
            available_space = 0;
        }
    }

    // printk("available space before performing read: %d\n", available_space);
    // printk("write offset after trace_buffer_read called before performing read: %d\n", filep->trace_buffer->offp_write);
    // printk("read offset after trace_buffer_read called before performing read: %d\n", filep->trace_buffer->offp_read);

    if (available_space != 0)
    {
        if (available_space >= count)
        {
            if (count <= 4096 - filep->trace_buffer->offp_read)
            {
                for (int i = filep->trace_buffer->offp_read; i < filep->trace_buffer->offp_read + count; i++)
                {
                    buff[i - filep->trace_buffer->offp_read] = filep->trace_buffer->trace_mem[i];
                }
                if (filep->trace_buffer->offp_read != 4096 - count)
                {
                    filep->trace_buffer->offp_read = filep->trace_buffer->offp_read + count;
                }
                else
                {
                    filep->trace_buffer->offp_read = 0;
                }
            }
            else
            {
                for (int i = filep->trace_buffer->offp_read; i < 4096; i++)
                {
                    buff[i - filep->trace_buffer->offp_read] = filep->trace_buffer->trace_mem[i];
                }
                for (int i = 0; i < count - 4096 + filep->trace_buffer->offp_read; i++)
                {
                    buff[i + 4096 - filep->trace_buffer->offp_read] = filep->trace_buffer->trace_mem[i];
                }
                filep->trace_buffer->offp_read = count - 4096 + filep->trace_buffer->offp_read;
            }
            if (filep->trace_buffer->offp_write == filep->trace_buffer->offp_read)
            {
                filep->trace_buffer->check_fullness = 0;
            }
            // printk("count(when count>avaialable): %d\n", count);
            // printk("write offset after trace_buffer_read called: %d\n", filep->trace_buffer->offp_write);
            // printk("read offset after trace_buffer_read called: %d\n", filep->trace_buffer->offp_read);
            return count;
        }
        else
        {
            if (available_space <= 4096 - filep->trace_buffer->offp_read)
            {
                for (int i = filep->trace_buffer->offp_read; i < filep->trace_buffer->offp_read + available_space; i++)
                {
                    buff[i - filep->trace_buffer->offp_read] = filep->trace_buffer->trace_mem[i];
                }
                if (filep->trace_buffer->offp_read != 4096 - available_space)
                {
                    filep->trace_buffer->offp_read = filep->trace_buffer->offp_read + available_space;
                }
                else
                {
                    filep->trace_buffer->offp_read = 0;
                }
            }
            else
            {
                for (int i = filep->trace_buffer->offp_read; i < 4096; i++)
                {
                    buff[i - filep->trace_buffer->offp_read] = filep->trace_buffer->trace_mem[i];
                }
                for (int i = 0; i < available_space - 4096 + filep->trace_buffer->offp_read; i++)
                {
                    buff[i + 4096 - filep->trace_buffer->offp_read] = filep->trace_buffer->trace_mem[i];
                }
                for (int i = available_space; i < count; i++)
                {
                    buff[i] = 0;
                }
                filep->trace_buffer->offp_read = available_space - 4096 + filep->trace_buffer->offp_read;
            }
            if (filep->trace_buffer->offp_write == filep->trace_buffer->offp_read)
            {
                filep->trace_buffer->check_fullness = 0;
            }
            count = available_space;
            // printk("count(when count<avaialable): %d\n", count);
            // printk("write offset after trace_buffer_read called: %d\n", filep->trace_buffer->offp_write);
            // printk("read offset after trace_buffer_read called: %d\n", filep->trace_buffer->offp_read);
            return count;
        }
    }
    else
    {
        return 0;
    }
}

int trace_buffer_read_dif(struct file *filep, char *buff, u32 count)
{
    // Check if the filep is valid and is associated with a trace buffer
    if (!filep || filep->type != TRACE_BUFFER)
    {
        return -EINVAL;
    }

    u32 available_space = 0;

    if (filep->trace_buffer->offp_read < filep->trace_buffer->offp_write)
    {
        available_space = filep->trace_buffer->offp_write - filep->trace_buffer->offp_read;
    }
    else if (filep->trace_buffer->offp_read > filep->trace_buffer->offp_write)
    {
        available_space = 4096 - filep->trace_buffer->offp_read + filep->trace_buffer->offp_write;
    }
    else
    {
        available_space = 4096 - filep->trace_buffer->offp_read + filep->trace_buffer->offp_write;
        if (filep->trace_buffer->check_fullness == 0)
        {
            available_space = 0;
        }
    }

    // printk("available space before performing read_strace: %d\n", available_space);
    // printk("write offset after trace_buffer_read called before performing read_strace: %d\n", filep->trace_buffer->offp_write);
    // printk("read offset after trace_buffer_read called before performing read_strace: %d\n", filep->trace_buffer->offp_read);

    if (available_space != 0)
    {
        if (available_space >= count)
        {
            if (count <= 4096 - filep->trace_buffer->offp_read)
            {
                for (int i = filep->trace_buffer->offp_read; i < filep->trace_buffer->offp_read + count; i++)
                {
                    buff[i - filep->trace_buffer->offp_read] = filep->trace_buffer->trace_mem[i];
                }
                if (filep->trace_buffer->offp_read != 4096 - count)
                {
                    filep->trace_buffer->offp_read = filep->trace_buffer->offp_read + count;
                }
                else
                {
                    filep->trace_buffer->offp_read = 0;
                }
            }
            else
            {
                for (int i = filep->trace_buffer->offp_read; i < 4096; i++)
                {
                    buff[i - filep->trace_buffer->offp_read] = filep->trace_buffer->trace_mem[i];
                }
                for (int i = 0; i < count - 4096 + filep->trace_buffer->offp_read; i++)
                {
                    buff[i + 4096 - filep->trace_buffer->offp_read] = filep->trace_buffer->trace_mem[i];
                }
                filep->trace_buffer->offp_read = count - 4096 + filep->trace_buffer->offp_read;
            }
            if (filep->trace_buffer->offp_write == filep->trace_buffer->offp_read)
            {
                filep->trace_buffer->check_fullness = 0;
            }
            // printk("count(when count>avaialable): %d\n", count);
            // printk("write offset after trace_buffer_read_dif called: %d\n", filep->trace_buffer->offp_write);
            // printk("read offset after trace_buffer_read_dif called: %d\n", filep->trace_buffer->offp_read);
            return count;
        }
        else
        {
            if (available_space <= 4096 - filep->trace_buffer->offp_read)
            {
                for (int i = filep->trace_buffer->offp_read; i < filep->trace_buffer->offp_read + available_space; i++)
                {
                    buff[i - filep->trace_buffer->offp_read] = filep->trace_buffer->trace_mem[i];
                }
                if (filep->trace_buffer->offp_read != 4096 - available_space)
                {
                    filep->trace_buffer->offp_read = filep->trace_buffer->offp_read + available_space;
                }
                else
                {
                    filep->trace_buffer->offp_read = 0;
                }
            }
            else
            {
                for (int i = filep->trace_buffer->offp_read; i < 4096; i++)
                {
                    buff[i - filep->trace_buffer->offp_read] = filep->trace_buffer->trace_mem[i];
                }
                for (int i = 0; i < available_space - 4096 + filep->trace_buffer->offp_read; i++)
                {
                    buff[i + 4096 - filep->trace_buffer->offp_read] = filep->trace_buffer->trace_mem[i];
                }
                for (int i = available_space; i < count; i++)
                {
                    buff[i] = 0;
                }
                filep->trace_buffer->offp_read = available_space - 4096 + filep->trace_buffer->offp_read;
            }
            if (filep->trace_buffer->offp_write == filep->trace_buffer->offp_read)
            {
                filep->trace_buffer->check_fullness = 0;
            }
            count = available_space;
            // printk("count(when count<avaialable): %d\n", count);
            // printk("write offset after trace_buffer_read_dif called: %d\n", filep->trace_buffer->offp_write);
            // printk("read offset after trace_buffer_read_dif called: %d\n", filep->trace_buffer->offp_read);
            return count;
        }
    }
    else
    {
        return 0;
    }
}

int trace_buffer_write(struct file *filep, char *buff, u32 count)
{ // Check if the filep is valid and is associated with a trace buffer

    // printk("trace_buffer_write called.\n");

    if (!filep || filep->type != TRACE_BUFFER)
    {
        return -EINVAL;
    }

    if (is_valid_mem_range((unsigned long)buff, count, 1) != 0)
    {
        // The buffer is not valid for write access
        return -EBADMEM;
    }

    u32 available_space = 0;

    if (filep->trace_buffer->offp_read > filep->trace_buffer->offp_write)
    {
        available_space = filep->trace_buffer->offp_read - filep->trace_buffer->offp_write;
    }
    else if (filep->trace_buffer->offp_read < filep->trace_buffer->offp_write)
    {
        available_space = 4096 - filep->trace_buffer->offp_write + filep->trace_buffer->offp_read;
    }
    else
    {
        available_space = 4096 - filep->trace_buffer->offp_write + filep->trace_buffer->offp_read;
        if (filep->trace_buffer->check_fullness == 1)
        {
            available_space = 0;
        }
    }

    // printk("available space before performing write: %d\n", available_space);
    // printk("write offset before performing write: %d\n", filep->trace_buffer->offp_write);
    // printk("read offset before performing write: %d\n", filep->trace_buffer->offp_read);

    if (filep->mode == O_WRITE || filep->mode == O_RDWR)
    {
        if (available_space != 0)
        {
            if (available_space >= count)
            {
                if (count <= 4096 - filep->trace_buffer->offp_write)
                {
                    for (int i = filep->trace_buffer->offp_write; i < filep->trace_buffer->offp_write + count; i++)
                    {
                        filep->trace_buffer->trace_mem[i] = buff[i - filep->trace_buffer->offp_write];
                    }
                    if (filep->trace_buffer->offp_write != 4096 - count)
                    {
                        filep->trace_buffer->offp_write = filep->trace_buffer->offp_write + count;
                    }
                    else
                    {
                        filep->trace_buffer->offp_write = 0;
                    }
                }
                else
                {
                    for (int i = filep->trace_buffer->offp_write; i < 4096; i++)
                    {

                        filep->trace_buffer->trace_mem[i] = buff[i - filep->trace_buffer->offp_write];
                    }
                    for (int i = 0; i < count - 4096 + filep->trace_buffer->offp_write; i++)
                    {

                        filep->trace_buffer->trace_mem[i] = buff[i + 4096 - filep->trace_buffer->offp_write];
                    }
                    filep->trace_buffer->offp_write = count - 4096 + filep->trace_buffer->offp_write;
                }
                if (filep->trace_buffer->offp_write == filep->trace_buffer->offp_read)
                {
                    filep->trace_buffer->check_fullness = 1;
                }

                // printk("count( when count > avaialable ): %d\n", count);
                // printk("write offset after trace_buffer_write called: %d\n", filep->trace_buffer->offp_write);
                // printk("read offset after trace_buffer_write called: %d\n", filep->trace_buffer->offp_read);

                return count;
            }
            else
            {
                if (available_space <= 4096 - filep->trace_buffer->offp_write)
                {
                    for (int i = filep->trace_buffer->offp_write; i < filep->trace_buffer->offp_write + available_space; i++)
                    {

                        filep->trace_buffer->trace_mem[i] = buff[i - filep->trace_buffer->offp_write];
                    }
                    if (filep->trace_buffer->offp_write != 4096 - available_space)
                    {
                        filep->trace_buffer->offp_write = filep->trace_buffer->offp_write + available_space;
                    }
                    else
                    {
                        filep->trace_buffer->offp_write = 0;
                    }
                }
                else
                {
                    for (int i = filep->trace_buffer->offp_write; i < 4096; i++)
                    {

                        filep->trace_buffer->trace_mem[i] = buff[i - filep->trace_buffer->offp_write];
                    }
                    for (int i = 0; i < available_space - 4096 + filep->trace_buffer->offp_write; i++)
                    {

                        filep->trace_buffer->trace_mem[i] = buff[i + 4096 - filep->trace_buffer->offp_write];
                    }
                    filep->trace_buffer->offp_write = available_space - 4096 + filep->trace_buffer->offp_write;
                }
                if (filep->trace_buffer->offp_write == filep->trace_buffer->offp_read)
                {
                    filep->trace_buffer->check_fullness = 1;
                }

                count = available_space;
                // printk("write offset after trace_buffer_write called: %d\n", filep->trace_buffer->offp_write);
                // printk("read offset after trace_buffer_write called: %d\n", filep->trace_buffer->offp_read);

                return count;
            }
        }
        else
        {

            // printk("write offset after trace_buffer_write called: %d\n", filep->trace_buffer->offp_write);
            // printk("read offset after trace_buffer_write called: %d\n", filep->trace_buffer->offp_read);

            return 0;
        }
    }
}

int trace_buffer_write_dif(struct file *filep, char *buff, u32 count)
{ // Check if the filep is valid and is associated with a trace buffer

    // printk("trace_buffer_write_dif called.\n");

    if (!filep || filep->type != TRACE_BUFFER)
    {
        return -EINVAL;
    }

    u32 available_space = 0;

    if (filep->trace_buffer->offp_read > filep->trace_buffer->offp_write)
    {
        available_space = filep->trace_buffer->offp_read - filep->trace_buffer->offp_write;
    }
    else if (filep->trace_buffer->offp_read < filep->trace_buffer->offp_write)
    {
        available_space = 4096 - filep->trace_buffer->offp_write + filep->trace_buffer->offp_read;
    }
    else
    {
        available_space = 4096 - filep->trace_buffer->offp_write + filep->trace_buffer->offp_read;
        if (filep->trace_buffer->check_fullness == 1)
        {
            available_space = 0;
        }
    }

    // printk("available space before performing write_strace: %d\n", available_space);
    // printk("write offset before performing write_strace: %d\n", filep->trace_buffer->offp_write);
    // printk("read offset before performing write_strace: %d\n", filep->trace_buffer->offp_read);

    if (filep->mode == O_WRITE || filep->mode == O_RDWR)
    {
        if (available_space != 0)
        {
            if (available_space >= count)
            {
                if (count <= 4096 - filep->trace_buffer->offp_write)
                {
                    for (int i = filep->trace_buffer->offp_write; i < filep->trace_buffer->offp_write + count; i++)
                    {

                        filep->trace_buffer->trace_mem[i] = buff[i - filep->trace_buffer->offp_write];
                    }
                    if (filep->trace_buffer->offp_write != 4096 - count)
                    {
                        filep->trace_buffer->offp_write = filep->trace_buffer->offp_write + count;
                    }
                    else
                    {
                        filep->trace_buffer->offp_write = 0;
                    }
                }
                else
                {
                    for (int i = filep->trace_buffer->offp_write; i < 4096; i++)
                    {

                        filep->trace_buffer->trace_mem[i] = buff[i - filep->trace_buffer->offp_write];
                    }
                    for (int i = 0; i < count - 4096 + filep->trace_buffer->offp_write; i++)
                    {

                        filep->trace_buffer->trace_mem[i] = buff[i + 4096 - filep->trace_buffer->offp_write];
                    }
                    filep->trace_buffer->offp_write = count - 4096 + filep->trace_buffer->offp_write;
                }
                if (filep->trace_buffer->offp_write == filep->trace_buffer->offp_read)
                {
                    filep->trace_buffer->check_fullness = 1;
                }

                // printk("count( when count > avaialable ): %d\n", count);
                // printk("write offset after trace_buffer_write_dif called: %d\n", filep->trace_buffer->offp_write);
                // printk("read offset after trace_buffer_write_dif called: %d\n", filep->trace_buffer->offp_read);

                return count;
            }
            else
            {
                if (available_space <= 4096 - filep->trace_buffer->offp_write)
                {
                    for (int i = filep->trace_buffer->offp_write; i < filep->trace_buffer->offp_write + available_space; i++)
                    {

                        filep->trace_buffer->trace_mem[i] = buff[i - filep->trace_buffer->offp_write];
                    }
                    if (filep->trace_buffer->offp_write != 4096 - available_space)
                    {
                        filep->trace_buffer->offp_write = filep->trace_buffer->offp_write + available_space;
                    }
                    else
                    {
                        filep->trace_buffer->offp_write = 0;
                    }
                }
                else
                {
                    for (int i = filep->trace_buffer->offp_write; i < 4096; i++)
                    {

                        filep->trace_buffer->trace_mem[i] = buff[i - filep->trace_buffer->offp_write];
                    }
                    for (int i = 0; i < available_space - 4096 + filep->trace_buffer->offp_write; i++)
                    {

                        filep->trace_buffer->trace_mem[i] = buff[i + 4096 - filep->trace_buffer->offp_write];
                    }
                    filep->trace_buffer->offp_write = available_space - 4096 + filep->trace_buffer->offp_write;
                }
                if (filep->trace_buffer->offp_write == filep->trace_buffer->offp_read)
                {
                    filep->trace_buffer->check_fullness = 1;
                }

                count = available_space;
                // printk("write offset after trace_buffer_write_dif called: %d\n", filep->trace_buffer->offp_write);
                // printk("read offset after trace_buffer_write_dif called: %d\n", filep->trace_buffer->offp_read);

                return count;
            }
        }
        else
        {

            // printk("write offset after trace_buffer_write_dif called: %d\n", filep->trace_buffer->offp_write);
            // printk("read offset after trace_buffer_write_dif called: %d\n", filep->trace_buffer->offp_read);

            return 0;
        }
    }
}

int sys_create_trace_buffer(struct exec_context *current, int mode)
{
    int free_file_descp = -2;
    if (mode == O_READ || mode == O_WRITE || mode == O_RDWR)
    {
        // Finding the lowest free file descriptor available
        for (int i = 0; i < MAX_OPEN_FILES; i++)
        {
            if ((current->files)[i] == NULL)
            {
                free_file_descp = i;
                break;
            }
        }

        // If no free file descriptors are available
        if (free_file_descp == -2)
        {
            return -EINVAL;
        }

        // Allocating file object
        struct file *file_tb = (struct file *)os_alloc(sizeof(struct file));
        if (!file_tb)
        {
            // printk("file object galat\n");
            return -ENOMEM;
        }

        // Initializing the file object
        file_tb->type = TRACE_BUFFER;
        file_tb->mode = mode;
        file_tb->offp = 0;
        file_tb->ref_count = 1;
        file_tb->inode = NULL;
        file_tb->trace_buffer = NULL;
        file_tb->fops = NULL;

        // Allocating memory for a trace buffer object
        struct trace_buffer_info *trace_buffer = (struct trace_buffer_info *)os_page_alloc(USER_REG);
        if (!trace_buffer)
        {
            // os_free(file_tb, sizeof(struct file)); // Clean up and free allocated memory
            //  printk("trace buffer galat\n");
            return -ENOMEM;
        }

        // Initializing the trace buffer
        trace_buffer->offp_read = 0;
        trace_buffer->offp_write = 0;
        trace_buffer->trace_mem = (char *)os_page_alloc(USER_REG);
        trace_buffer->check_fullness = 0;
        // for (int i = 0;  i< 4096; i++){
        // 	trace_buffer->usage[i] = 0;
        // }
        if (trace_buffer->trace_mem == NULL)
        {
            // os_free(file_tb, sizeof(struct file)); // Clean up and free allocated memory
            //  printk("trace mem null hai \n");
            return -ENOMEM;
        }

        // Allocating memory for a fileops object
        struct fileops *fops = (struct fileops *)os_alloc(sizeof(struct fileops));
        if (!fops)
        {
            // os_free(trace_buffer, sizeof(struct trace_buffer_info)); // Clean up and free allocated memory
            // deos_free(file_tb, sizeof(struct file)); // Clean up and free allocated memory
            //  printk("file ops galat\n");
            return -ENOMEM;
        }

        // Initialize the function pointers in the fileops object
        fops->read = trace_buffer_read;
        fops->write = trace_buffer_write;
        fops->lseek = NULL; // You won't use lseek in this context
        fops->close = trace_buffer_close;

        // Update the file object's trace_buffer and fops fields
        file_tb->trace_buffer = trace_buffer;
        file_tb->fops = fops;

        current->files[free_file_descp] = file_tb;

        return free_file_descp;
    }
    else
    {
        return -EINVAL;
    }
}

///////////////////////////////////////////////////////////////////////////
//// 		Start of strace functionality 		      	      /////
///////////////////////////////////////////////////////////////////////////

u64 params_syscall(u64 syscall_num)
{

    // printk("Number for parameters for syscall number: %d\n", syscall_num);

    switch (syscall_num)
    {
        // getpid()
    case SYSCALL_GETPID:
        return 1;
        // getppid()
    case SYSCALL_GETPPID:
        return 1;

        /////////// ------->
        // shrink()
    case SYSCALL_SHRINK:
        return 4;
        // alarm()
    case SYSCALL_ALARM:
        return 2;
        //------->/////////

        // fork()
    case SYSCALL_FORK:
        return 1;
        // cfork()
    case SYSCALL_CFORK:
        return 1;
        // vfork()
    case SYSCALL_VFORK:
        return 1;
        // get_user_page_stats()
    case SYSCALL_GET_USER_P:
        return 1;
        // get_cow_fault_stats()
    case SYSCALL_GET_COW_F:
        return 1;
        // physinfo()
    case SYSCALL_PHYS_INFO:
        return 1;
        // get_stats()
    case SYSCALL_STATS:
        return 1;
        // end_strace()
    case SYSCALL_END_STRACE:
        return 1;
        // configure(struct os_configs*)
    case SYSCALL_CONFIGURE:
        return 2;
        // exit(int)
    case SYSCALL_EXIT:
        return 2;
        // sleep(int)
    case SYSCALL_SLEEP:
        return 2;
        // dump_page_table(char*)
    case SYSCALL_DUMP_PTT:
        return 2;
        // pmap(int)
    case SYSCALL_PMAP:
        return 2;
        // dup(int)
    case SYSCALL_DUP:
        return 2;
        // create_trace_buffer(int)
    case SYSCALL_TRACE_BUFFER:
        return 2;
        // close(int)
    case SYSCALL_CLOSE:
        return 2;
        // signal(int, void*)
    case SYSCALL_SIGNAL:
        return 3;
        // expand(unsigned, int)
    case SYSCALL_EXPAND:
        return 3;
        // clone(void*, long)
    case SYSCALL_CLONE:
        return 3;
        // munmap(void*, int)
    case SYSCALL_MUNMAP:
        return 3;
        // strace(int, int)
    case SYSCALL_STRACE:
        return 3;
        // start_strace(int, int)
    case SYSCALL_START_STRACE:
        return 3;
        // open(char*, int, ...)
    case SYSCALL_OPEN:
        return 3;
        // dup2(int, int)
    case SYSCALL_DUP2:
        return 3;
        // mprotect(void*, int, int)
    case SYSCALL_MPROTECT:
        return 4;
        // write(int, void*, int)
    case SYSCALL_WRITE:
        return 4;
        // read(int, void*, int)
    case SYSCALL_READ:
        return 4;
        // lseek(int, long, int)
    case SYSCALL_LSEEK:
        return 4;
        // read_strace(int, void*, int)
    case SYSCALL_READ_STRACE:
        return 4;
        // read_ftrace(int, void*, int)
    case SYSCALL_READ_FTRACE:
        return 4;
        // ftrace(unsigned long, long, long, int)
    case SYSCALL_FTRACE:
        return 5;
        // mmap(void*, int, int, int)
    case SYSCALL_MMAP:
        return 5;
        // default for unknown syscall number
    default:
        return 0;
    }
}

int perform_tracing(u64 syscall_num, u64 param1, u64 param2, u64 param3, u64 param4)
{
    struct exec_context *ctx = get_current_ctx();

    // printk("perform tracing called for syscall number: %d\n", syscall_num);

    // If there is some initialising error with st_md_base
    if (ctx->st_md_base == NULL)
    {
        // printk("st_md_base doesn't exist\n");
        return 0;
    }

    // If tracing is off
    if (ctx->st_md_base->is_traced == 0)
    {
        // printk("tracing is off for this st_md_base\n");
        return 0;
    }

    // If we trace untraceable calls -> strace, start_strace, end_strace
    if (syscall_num == SYSCALL_STRACE || syscall_num == SYSCALL_START_STRACE || syscall_num == SYSCALL_END_STRACE)
    {
        // printk("Tracing untraceable calls -> strace, start_strace, end_strace.\n");
        return 0;
    }

    // If mode is filtered tracing, we need to find out if this syscall is allowed to be traced by strace
    if (ctx->st_md_base->tracing_mode == FILTERED_TRACING)
    {
        int syscall_allowed = 0;
        struct strace_info *curr = ctx->st_md_base->next;
        while (curr != NULL)
        {
            if (curr->syscall_num == syscall_num)
            {
                syscall_allowed = 1;
                break;
            }
            curr = curr->next;
        }
        if (syscall_allowed == 0)
        {
            // printk("syscall not in strace so cant be read in filtered tracing mode.\n");
            return 0;
        }
    }

    // Find out all the parameters = syscall number + parameters passed before performing trace
    u64 num_of_params = params_syscall(syscall_num);

    // Unrecognised syscall
    if (num_of_params == 0)
    {
        return 0;
    }

    // Find the trace buffer file
    struct file *trace_buffer_file = ctx->files[ctx->st_md_base->strace_fd];

    // Make the temporary buffer to be passed to write call
    u64 *temp_buffer = (u64 *)os_page_alloc(USER_REG);

    // Define the delimiter and fill the temporary buffer with delimiter and syscall num
    temp_buffer[0] = syscall_num;
    // printk("temp buffer[0] = %d, the syscall num being stored in temp buffer\n",temp_buffer[0]);

    // Fill the rest of arguments
    u64 argv[5] = {syscall_num, param1, param2, param3, param4};
    for (int i = 1; i < num_of_params; i++)
    {
        temp_buffer[i] = argv[i];
        // printk("temp buffer[%d] = %d\n",i,temp_buffer[i]);
    }

    // Call the write sys call for writing in trace_buffer and free the temporray buffer
    trace_buffer_write_dif(trace_buffer_file, (char *)temp_buffer, (num_of_params)*8);
    os_page_free(USER_REG, temp_buffer);

    return 0;
}

int sys_strace(struct exec_context *current, int syscall_num, int action)
{
    if (current == NULL)
    {
        return -EINVAL;
    }

    if (action == ADD_STRACE)
    {

        if (current->st_md_base == NULL)
        {
            // printk("st_md_base is null. Allocating to start strace as action is ADD STRACE\n");
            current->st_md_base = os_alloc(sizeof(struct strace_head));
            current->st_md_base->count = 0;
            current->st_md_base->last = NULL;
            current->st_md_base->next = NULL;
        }

        struct strace_info *tmp = current->st_md_base->next;

        while (tmp != NULL)
        {
            if (tmp->syscall_num == syscall_num)
            {
                // printk("syscall number %d already exists in strace\n", syscall_num);
                return -EINVAL;
            }
            tmp = tmp->next;
        }

        if (current->st_md_base->count >= MAX_STRACE)
        {
            // printk("strace is already full cant add more\n");
            return -EINVAL;
        }
        else
        {

            struct strace_info *new = os_alloc(sizeof(struct strace_info));
            new->syscall_num = syscall_num;
            new->next = NULL;

            if (current->st_md_base->count == 0)
            {
                current->st_md_base->last = new;
                current->st_md_base->next = new;
            }
            else
            {
                current->st_md_base->last->next = new;
                current->st_md_base->last = new;
            }

            current->st_md_base->count += 1;
            // printk("count of st_md_base = %d after adding syscall num %d\n",current->st_md_base->count,syscall_num);
        }
    }
    else
    {
        if (current->st_md_base == NULL)
        {
            // printk("st_md_base is null. Dont have anything to delete -> while REMOVE_TRACE\n");
            return -EINVAL;
        }
        else
        {
            struct strace_info *tmp = current->st_md_base->next;
            struct strace_info *prev = NULL;

            while (tmp != NULL)
            {
                if (tmp->syscall_num == syscall_num)
                {
                    if (prev == NULL)
                    {
                        current->st_md_base->next = tmp->next;
                        os_free(tmp, sizeof(struct strace_info));
                    }
                    else
                    {
                        prev->next = tmp->next;
                        os_free(tmp, sizeof(struct strace_info));
                    }
                    current->st_md_base->count -= 1;
                    return 0;
                }
                prev = tmp;
                tmp = tmp->next;
            }

            return -EINVAL;
        }
    }
    return 0;
}

int sys_read_strace(struct file *filep, char *buff, u64 count)
{
    if (filep == NULL)
    {
        return -EINVAL;
    }

    int read_bytes = 0;
    while (count--)
    {

        if (filep->trace_buffer->offp_read == filep->trace_buffer->offp_write)
        {
            // printk("trace buffer read fully already\n");
            return read_bytes;
        }

        trace_buffer_read_dif(filep, buff + read_bytes, 8);
        read_bytes = read_bytes + 8;

        u64 syscall_num = ((u64 *)buff)[read_bytes / 8 - 1];
        u64 num_of_params = params_syscall(syscall_num);
        // printk("syscall : %d\n", syscall_num);

        for (u64 j = 0; j < num_of_params - 1; j++)
        {
            trace_buffer_read_dif(filep, buff + read_bytes, 8);
            read_bytes += 8;
        }
    }
    return read_bytes;
}

int sys_start_strace(struct exec_context *current, int fd, int tracing_mode)
{
    if (fd < 0 || (tracing_mode != FULL_TRACING && tracing_mode != FILTERED_TRACING) || current == NULL)
    {
        return -EINVAL; // Invalid arguments
    }

    if (current->st_md_base == NULL)
    {
        // printk("Current process is getting its strace base allocated.\n");
        current->st_md_base = os_alloc(sizeof(struct strace_head));
        current->st_md_base->count = 0;
        current->st_md_base->is_traced = 1;
        current->st_md_base->last = NULL;
        current->st_md_base->next = NULL;
        current->st_md_base->strace_fd = fd;
        current->st_md_base->tracing_mode = tracing_mode;
    }
    else
    {
        // printk("Current process is has an strace base, needs to be updated.\n");
        current->st_md_base->is_traced = 1;
        current->st_md_base->strace_fd = fd;
        current->st_md_base->tracing_mode = tracing_mode;
    }
    return 0;
}

int sys_end_strace(struct exec_context *current)
{
    if (current == NULL || current->st_md_base == NULL)
    {
        return -EINVAL;
    }

    struct strace_info *tmp;
    struct strace_info *head = current->st_md_base->next;

    while (head != NULL)
    {
        tmp = head;
        head = head->next;
        os_free(tmp, sizeof(struct strace_info));
    }

    os_free(current->st_md_base, sizeof(struct strace_head));
    current->st_md_base = NULL;

    if (current->st_md_base == NULL)
    {
        // printk("head emptied\n");
    }
    return 0;
}

///////////////////////////////////////////////////////////////////////////
//// 		Start of ftrace functionality 		      	      /////
///////////////////////////////////////////////////////////////////////////

long do_ftrace(struct exec_context *ctx, unsigned long faddr, long action, long nargs, int fd_trace_buffer)
{
    if (ctx == NULL)
    {
        return -EINVAL;
    }
    if (nargs < 0 || fd_trace_buffer < 0)
    {
        return -EINVAL;
    }
    if (action == ADD_FTRACE)
    {
        if (ctx->ft_md_base == NULL)
        {
            ctx->ft_md_base = (struct ftrace_head *)os_alloc(sizeof(struct ftrace_head));
            ctx->ft_md_base->count = 0;
            ctx->ft_md_base->next = NULL;
            ctx->ft_md_base->last = NULL;
        }

        struct ftrace_info *tmp = ctx->ft_md_base->next;

        while (tmp != NULL)
        {
            if (tmp->faddr == faddr)
            {
                // printk("function addr %d already exists in ftrace\n", faddr);
                return -EINVAL;
            }
            tmp = tmp->next;
        }

        if (ctx->ft_md_base->count >= MAX_FTRACE)
        {
            // printk("ftrace is already full cant add more\n");
            return -EINVAL;
        }
        else
        {

            struct ftrace_info *new = os_alloc(sizeof(struct ftrace_info));
            new->faddr = faddr;
            new->fd = fd_trace_buffer;
            new->num_args = nargs;
            new->next = NULL;
            new->capture_backtrace = 0;

            if (ctx->ft_md_base->count == 0)
            {
                ctx->ft_md_base->last = new;
                ctx->ft_md_base->next = new;
            }
            else
            {
                ctx->ft_md_base->last->next = new;
                ctx->ft_md_base->last = new;
            }

            ctx->ft_md_base->count += 1;
            // printk("count of ft_md_base = %d after adding function faddr %d\n",ctx->ft_md_base->count,faddr);

            return 0;
        }
    }
    else if (action == REMOVE_FTRACE)
    {
        if (ctx->ft_md_base == NULL)
        {
            return -EINVAL;
        }
        else
        {
            struct ftrace_info *tmp = ctx->ft_md_base->next;
            struct ftrace_info *prev = NULL;

            while (tmp != NULL)
            {
                if (tmp->faddr == faddr)
                {
                    if (prev == NULL)
                    {
                        ctx->ft_md_base->next = tmp->next;
                        os_free(tmp, sizeof(struct ftrace_info));
                    }
                    else
                    {
                        prev->next = tmp->next;
                        os_free(tmp, sizeof(struct ftrace_info));
                    }
                    ctx->ft_md_base->count -= 1;
                    // printk("count of ft_md_base = %d after removing function faddr %d\n",ctx->ft_md_base->count,faddr);
                    return 0;
                }
                prev = tmp;
                tmp = tmp->next;
            }

            return -EINVAL;
        }
    }
    else if (action == ENABLE_FTRACE)
    {

        if (ctx == NULL || ctx->ft_md_base == NULL)
        {
            return -EINVAL;
        }

        struct ftrace_info *tmp = ctx->ft_md_base->next;

        while (tmp != NULL)
        {
            if (tmp->faddr == faddr)
            {
                if (!(((u8 *)(faddr))[0] == INV_OPCODE && ((u8 *)(faddr))[1] == INV_OPCODE && ((u8 *)(faddr))[2] == INV_OPCODE && ((u8 *)(faddr))[3] == INV_OPCODE))
                {
                    tmp->code_backup[0] = ((u8 *)(faddr))[0];
                    tmp->code_backup[1] = ((u8 *)(faddr))[1];
                    tmp->code_backup[2] = ((u8 *)(faddr))[2];
                    tmp->code_backup[3] = ((u8 *)(faddr))[3];
                    // printk("Trace Enabled.\n");
                }

                ((u8 *)(faddr))[0] = INV_OPCODE;
                ((u8 *)(faddr))[1] = INV_OPCODE;
                ((u8 *)(faddr))[2] = INV_OPCODE;
                ((u8 *)(faddr))[3] = INV_OPCODE;

                return 0;
            }
            tmp = tmp->next;
        }
        return -EINVAL;
    }
    else if (action == DISABLE_FTRACE)
    {

        if (ctx == NULL || ctx->ft_md_base == NULL)
        {
            return -EINVAL;
        }

        struct ftrace_info *tmp = ctx->ft_md_base->next;

        while (tmp != NULL)
        {
            if (tmp->faddr == faddr)
            {
                if (((u8 *)(faddr))[0] == INV_OPCODE && ((u8 *)(faddr))[1] == INV_OPCODE && ((u8 *)(faddr))[2] == INV_OPCODE && ((u8 *)(faddr))[3] == INV_OPCODE)
                {
                    ((u8 *)(faddr))[0] = tmp->code_backup[0];
                    ((u8 *)(faddr))[1] = tmp->code_backup[1];
                    ((u8 *)(faddr))[2] = tmp->code_backup[2];
                    ((u8 *)(faddr))[3] = tmp->code_backup[3];
                    // printk("Trace Disabled.\n");
                }
                return 0;
            }
            tmp = tmp->next;
        }
        return -EINVAL;
    }
    else if (action == ENABLE_BACKTRACE)
    {

        if (ctx == NULL || ctx->ft_md_base == NULL)
        {
            return -EINVAL;
        }

        struct ftrace_info *tmp = ctx->ft_md_base->next;

        while (tmp != NULL)
        {
            if (tmp->faddr == faddr)
            {
                if (!(((u8 *)(faddr))[0] == INV_OPCODE && ((u8 *)(faddr))[1] == INV_OPCODE && ((u8 *)(faddr))[2] == INV_OPCODE && ((u8 *)(faddr))[3] == INV_OPCODE))
                {
                    tmp->code_backup[0] = ((u8 *)(faddr))[0];
                    tmp->code_backup[1] = ((u8 *)(faddr))[1];
                    tmp->code_backup[2] = ((u8 *)(faddr))[2];
                    tmp->code_backup[3] = ((u8 *)(faddr))[3];
                    // printk("BackTrace Enabled.\n");
                }

                ((u8 *)(faddr))[0] = INV_OPCODE;
                ((u8 *)(faddr))[1] = INV_OPCODE;
                ((u8 *)(faddr))[2] = INV_OPCODE;
                ((u8 *)(faddr))[3] = INV_OPCODE;

                tmp->capture_backtrace = 1;

                return 0;
            }
            tmp = tmp->next;
        }
        return -EINVAL;
    }
    else if (action == DISABLE_BACKTRACE)
    {

        if (ctx == NULL || ctx->ft_md_base == NULL)
        {
            return -EINVAL;
        }

        struct ftrace_info *tmp = ctx->ft_md_base->next;

        while (tmp != NULL)
        {
            if (tmp->faddr == faddr)
            {
                ((u8 *)(faddr))[0] = tmp->code_backup[0];
                ((u8 *)(faddr))[1] = tmp->code_backup[1];
                ((u8 *)(faddr))[2] = tmp->code_backup[2];
                ((u8 *)(faddr))[3] = tmp->code_backup[3];
                // printk("BackTrace Disabled.\n");
                tmp->capture_backtrace = 0;
                return 0;
            }
            tmp = tmp->next;
        }
        return -EINVAL;
    }
    else
    {
        return -EINVAL;
    }
}

// Fault handler
long handle_ftrace_fault(struct user_regs *regs)
{
    struct exec_context *ctx = get_current_ctx();

    if (ctx == NULL || ctx->ft_md_base == NULL)
    {
        return -EINVAL;
    }

    struct ftrace_info *tmp = ctx->ft_md_base->next;

    while (tmp != NULL)
    {
        if (tmp->faddr == regs->entry_rip)
        {

            // Find the trace buffer file
            struct file *trace_buffer_file = ctx->files[tmp->fd];

            // Make the temporary buffer to be passed to write call
            u64 *temp_buffer = (u64 *)os_page_alloc(USER_REG);
            int number_of_arguments_bt = 0;

            // Define the delimiter and fill the temporary buffer with delimiter and syscall num
            temp_buffer[0] = tmp->num_args + 1;
            // printk("temp buffer[0] = %d\n",temp_buffer[0]);
            temp_buffer[1] = regs->entry_rip;
            // printk("temp buffer[1] = %d\n",temp_buffer[1]);

            // Fill the rest of arguments
            u64 argv[6] = {regs->rdi, regs->rsi, regs->rdx, regs->rcx, regs->r8, regs->r9};
            for (int i = 2; i < tmp->num_args + 2; i++)
            {
                temp_buffer[i] = argv[i - 2];
                // printk("temp buffer[%d] = %d\n",i,temp_buffer[i]);
            }

            // Call the write sys call for writing in trace_buffer and free the temporray buffer

            if (tmp->capture_backtrace == 1)
            {
                temp_buffer[tmp->num_args + 2] = tmp->faddr;
                temp_buffer[0] += 1;
                // printk("temp buffer[0] = %d\n",temp_buffer[0]);
                if (*((u64 *)regs->entry_rsp) != END_ADDR)
                {
                    temp_buffer[tmp->num_args + 3] = *((u64 *)regs->entry_rsp);
                    temp_buffer[0] += 1;
                    // printk("temp buffer[0] = %d\n",temp_buffer[0]);
                }
                else
                {
                    trace_buffer_write_dif(trace_buffer_file, (char *)temp_buffer, (temp_buffer[0] + 1) * 8);
                    // printk("check_fullness = %d", trace_buffer_file->trace_buffer->check_fullness);
                    // printk("write offset after trace_buffer_write_dif called: %d\n", trace_buffer_file->trace_buffer->offp_write);
                    // printk("read offset after trace_buffer_write_dif called: %d\n", trace_buffer_file->trace_buffer->offp_read);
                    os_page_free(USER_REG, temp_buffer);

                    regs->entry_rsp -= 8;
                    *((u64 *)regs->entry_rsp) = regs->rbp;
                    regs->rbp = regs->entry_rsp;
                    regs->entry_rip += 4;
                    return 0;
                }
                u64 tmp_rbp = regs->rbp;
                int i = tmp->num_args + 4;
                while (*((u64 *)(tmp_rbp + 8)) != END_ADDR)
                {
                    temp_buffer[i] = *((u64 *)(tmp_rbp + 8));
                    tmp_rbp = *((u64 *)tmp_rbp);
                    i = i + 1;
                    temp_buffer[0] += 1;
                    // printk("temp buffer[0] = %d\n temp_buffer[%d] = %d",temp_buffer[0], i, temp_buffer[i]);
                }
            }

            trace_buffer_write_dif(trace_buffer_file, (char *)temp_buffer, (temp_buffer[0] + 1) * 8);
            // printk("check_fullness = %d\n", trace_buffer_file->trace_buffer->check_fullness);
            // printk("write offset after trace_buffer_write_dif called: %d\n", trace_buffer_file->trace_buffer->offp_write);
            // printk("read offset after trace_buffer_write_dif called: %d\n", trace_buffer_file->trace_buffer->offp_read);
            os_page_free(USER_REG, temp_buffer);

            regs->entry_rsp -= 8;
            *((u64 *)regs->entry_rsp) = regs->rbp;
            regs->rbp = regs->entry_rsp;
            regs->entry_rip += 4;
            return 0;
        }
        tmp = tmp->next;
    }
    return -EINVAL;
}

int sys_read_ftrace(struct file *filep, char *buff, u64 count)
{
    if (filep == NULL)
    {
        return -EINVAL;
    }

    int read_bytes = 0;
    u64 *tmp_no_of_args = (u64 *)os_page_alloc(USER_REG);
    while (count--)
    {

        if (filep->trace_buffer->offp_read == filep->trace_buffer->offp_write && filep->trace_buffer->check_fullness == 0)
        {
            // printk("trace buffer read fully already\n");
            return read_bytes;
        }

        trace_buffer_read_dif(filep, (char *)tmp_no_of_args, 8);

        u64 no_of_args = ((u64 *)tmp_no_of_args)[0];
        // printk("syscall : %d\n", no_of_args);

        for (u64 j = 0; j < no_of_args; j++)
        {
            trace_buffer_read_dif(filep, buff + read_bytes, 8);
            read_bytes += 8;
        }
    }
    return read_bytes;
}