/*
* Open Chinese Convert
*
* Copyright 2010 BYVoid <byvoid1@gmail.com>
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*      http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#include "opencc_dictionary_datrie.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

typedef enum
{
	MEMORY_TYPE_MMAP,
	MEMORY_TYPE_ALLOCATE
} memory_type;

typedef struct
{
	const DoubleArrayTrieItem * dat;
	size_t dat_item_count;
	const wchar_t * lexicon;
	size_t lexicon_length;

	void * dic_memory;
	size_t dic_size;
	memory_type dic_memory_type;

} datrie_dictionary;

static int load_allocate(datrie_dictionary * dd, int fd)
{
	dd->dic_memory_type = MEMORY_TYPE_ALLOCATE;
	dd->dic_memory = malloc(dd->dic_size);
	if (dd->dic_memory == NULL)
	{
		/* 內存申請失敗 */
		return -1;
	}
	lseek(fd, 0, SEEK_SET);
	read(fd, dd->dic_memory, dd->dic_size);
	return 0;
}

static int load_mmap(datrie_dictionary * dd, int fd)
{
	dd->dic_memory_type = MEMORY_TYPE_MMAP;
	dd->dic_memory = mmap (NULL, dd->dic_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (dd->dic_memory == MAP_FAILED)
	{
		/* 內存映射創建失敗 */
		dd->dic_memory = NULL;
		return -1;
	}
	return 0;
}

static int load_dict(datrie_dictionary * dd, FILE * fp)
{
	int fd = fileno(fp);

	fseek(fp, 0, SEEK_END);
	dd->dic_size = ftell(fp);

	/* 首先嘗試mmap，如果失敗嘗試申請內存 */
	if (load_mmap(dd, fd) == -1)
	{
		if (load_allocate(dd, fd) == -1)
		{
			return -1;
		}
	}

	size_t header_len = strlen(OPENCC_DICHEADER);

	if (strncmp((const char *)dd->dic_memory, OPENCC_DICHEADER, header_len) != 0)
	{
		/* TODO 文件頭校驗失敗 */
		return -1;
	}

	size_t offset = 0;

	offset += header_len * sizeof(char);

	dd->lexicon_length = *((size_t *) (dd->dic_memory + offset));
	offset += sizeof(size_t);

	dd->dat_item_count = *((size_t *) (dd->dic_memory + offset));
	offset += sizeof(size_t);

	size_t lexicon_size = dd->lexicon_length * sizeof(wchar_t);
	/* size_t dat_size = dd->dat_item_count * sizeof(DoubleArrayTrieItem); */

	dd->lexicon = (wchar_t *) (dd->dic_memory + offset);

	offset += lexicon_size;

	dd->dat = (DoubleArrayTrieItem * ) (dd->dic_memory + offset);

	return 0;
}

static int unload_dict(datrie_dictionary * dd)
{
	if (dd->dic_memory != NULL)
	{
		if (MEMORY_TYPE_MMAP == dd->dic_memory_type)
		{
			return munmap(dd->dic_memory, dd->dic_size);
		}
		else if (MEMORY_TYPE_ALLOCATE == dd->dic_memory_type)
		{
			free(dd->dic_memory);
		}
		else
		{
			return -1;
		}
	}
	return 0;
}

dict_ptr dict_datrie_open(const char * filename)
{
	datrie_dictionary * dd = (datrie_dictionary *) malloc(sizeof(datrie_dictionary));
	dd->dat = NULL;
	dd->lexicon = NULL;

	FILE * fp = fopen(filename, "rb");

	if (load_dict(dd, fp) == -1)
	{
		dict_datrie_close((dict_ptr) dd);
		return (dict_ptr) -1;
	}

	fclose(fp);

	return (dict_ptr) dd;
}

int dict_datrie_close(dict_ptr dp)
{
	datrie_dictionary * dd = (datrie_dictionary *) dp;

	if (unload_dict(dd) == -1)
	{
		free(dd);
		return -1;
	}

	free(dd);
	return 0;
}

int encode_char(wchar_t ch)
{
	return (int)ch;
}

void datrie_match(const datrie_dictionary * dd, const wchar_t * word,
		size_t *match_pos, size_t *id, size_t limit)
{
	size_t i, p;
	for (i = 0,p = 0; word[p] && (limit == 0 || p < limit) &&
			dd->dat[i].base != DATRIE_UNUSED; p ++)
	{
		int k = encode_char(word[p]);
		int j = dd->dat[i].base + k;
		if (j < 0 || j >= dd->dat_item_count || dd->dat[j].parent != i)
			break;
		i = j;
	}
	if (match_pos)
		*match_pos = p;
	if (id)
		*id = i;
}

const wchar_t * dict_datrie_match_longest(dict_ptr dp, const wchar_t * word,
		size_t length)
{
	datrie_dictionary * dd = (datrie_dictionary *) dp;

	size_t pos, item;
	datrie_match(dd, word, &pos, &item, length);

	while (dd->dat[item].word == -1 && pos > 1)
		datrie_match(dd, word, &pos, &item, pos - 1);

	if (pos == 0 || dd->dat[item].word == -1)
		return NULL;

	return dd->lexicon + dd->dat[item].word;
}

size_t dict_datrie_get_all_match_lengths(dict_ptr dp, const wchar_t * word,
		size_t * match_length)
{
	datrie_dictionary * dd = (datrie_dictionary *) dp;

	size_t rscnt = 0;

	size_t i, p;
	for (i = 0,p = 0; word[p] && dd->dat[i].base != DATRIE_UNUSED; p ++)
	{
		int k = encode_char(word[p]);
		int j = dd->dat[i].base + k;
		if (j < 0 || j >= dd->dat_item_count || dd->dat[j].parent != i)
			break;
		i = j;

		if (dd->dat[i].word != -1)
			match_length[rscnt ++] = p + 1;
	}

	return rscnt;
}

size_t dict_datrie_get_lexicon(dict_ptr dp, opencc_entry * lexicon)
{
	/* TODO 從datrie中讀取詞條 */
	return 0;
}
