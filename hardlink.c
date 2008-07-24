#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0500
#include <assert.h>
#include <windows.h>
#include <tchar.h>
#include <stdlib.h>
#include <stdio.h>
#include "md5.h"

#define PATHSIZE (MAX_PATH + 10)


int files_scanned = 0,
    files_linked  = 0;
long long bytes_saved    = 0;

typedef struct LeafNode
{
	md5_byte_t key[16];
	LPCTSTR value;
} LeafNode;

typedef struct BranchNode
{	
	int position;
	struct Node *left, *right;
} BranchNode;

typedef struct Node
{
	enum { LEAF, BRANCH } type;
	union {
		struct LeafNode leaf;
		struct BranchNode branch;
	};
} Node;

Node *root;

Node *CreateLeafNode(md5_byte_t key[16], LPCTSTR value)
{
	Node *node = malloc(sizeof(Node));

	assert(node != NULL);

	node->type = LEAF;
	memcpy(node->leaf.key, key, sizeof(node->leaf.key));
	node->leaf.value = _tcsdup(value);

	return node;
}

Node *FindNode(md5_byte_t key[16], Node *node)
{
	if (node->type == LEAF)
	{
		return node;
	}
	else
	{
		int p = node->branch.position;
		return FindNode(key, (key[p/8] & (1<<(p%8))) ? node->branch.right : node->branch.left);
	}
}

void InsertNode(Node *newNode, int position, Node **node)
{
	if((*node)->type == BRANCH && (*node)->branch.position <= position)
	{
		int p = (*node)->branch.position;
		assert(p < position);
		InsertNode( newNode, position, (newNode->leaf.key[p/8] & (1<<(p%8))) ?
			&(*node)->branch.right : &(*node)->branch.left );
	}
	else
	{
		Node *branchNode = malloc(sizeof(Node));
		assert(branchNode != NULL);
		branchNode->type = BRANCH;
		branchNode->branch.position = position;
		if (newNode->leaf.key[position/8] & (1<<(position%8)))
		{
			branchNode->branch.left  = *node;
			branchNode->branch.right = newNode;
		}
		else
		{
			branchNode->branch.left  = newNode;
			branchNode->branch.right = *node;
		}
		*node = branchNode;
	}
}

int Position(md5_byte_t a[16], md5_byte_t b[16])
{
	int n, m;

	for (n = 0; n < 16; ++n)
		for (m = 0; m < 8; ++m)
			if ((a[n] ^ b[n]) & (1 << m))
				return 8*n + m;

	assert(memcmp(a, b, 16) == 0);
	return -1;
}

void IdentifyFile(LPCTSTR filePath)
{
	md5_byte_t buffer[16384], digest[16];
	md5_state_t pms;
	HANDLE hFile;
	DWORD dwBytesRead;
	long long llFileSize;

	hFile = CreateFile(filePath, FILE_READ_DATA, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
	{
		fprintf(stderr, "Could not open file ``%ls'' for reading!\n", filePath);
		return;
	}

	/* Read file and compute digest */
	md5_init(&pms);
	llFileSize = 0;
	do {
		if (!ReadFile(hFile, buffer, sizeof(buffer), &dwBytesRead, NULL))
		{
			fprintf(stderr, "Reading from file ``%ls'' failed!\n", filePath);
			return;
		}
		llFileSize += dwBytesRead;
		md5_append(&pms, buffer, dwBytesRead);
	} while (dwBytesRead);
	md5_finish(&pms, digest);
	CloseHandle(hFile);
	++files_scanned;

	/* Look-up in crit-bit tree */
	if(root == NULL)
	{
		root = CreateLeafNode(digest, filePath);
	}
	else
	{
		Node *node = FindNode(digest, root);
		int pos = Position(node->leaf.key, digest);

		assert(node->type == LEAF);
		if(pos >= 0)
		{
			InsertNode(CreateLeafNode(digest, filePath), Position(node->leaf.key, digest), &root);
		}
		else
		{
			LPCTSTR existingFilePath = node->leaf.value;
			TCHAR bakPath[PATHSIZE];

			printf("Replacing ``%ls'' with link to ``%ls''...\n", filePath, existingFilePath);
			_tcscpy(bakPath, filePath);
			_tcscat(bakPath, TEXT(".bak"));	
			if (!MoveFile(filePath, bakPath))
				fprintf(stderr, "Could not move file ``%ls'' to ``%ls''!\n", filePath, bakPath);
			else
			{
				if (!CreateHardLink(filePath, existingFilePath, NULL))
				{
					fprintf(stderr, "Could create hard link from ``%ls'' to ``%ls''!\n", filePath, existingFilePath);
					if (!MoveFile(bakPath, filePath))
						fprintf(stderr, "Could not move file ``%ls'' to ``%ls''!\n", bakPath, filePath);
				}
				else
				{
					++files_linked;
					bytes_saved += llFileSize;
					if (!DeleteFile(bakPath))
						fprintf(stderr, "Could not delete backup copy ``%ls''!\n", bakPath);
				}
			}
		}
	}
}

void ScanDirectory(LPCTSTR dirPath)
{
	TCHAR path[PATHSIZE], *file;
	WIN32_FIND_DATA wfd;
	HANDLE hFind;

	_tcscpy(path, dirPath);
	_tcscat(path, TEXT("\\*"));
	if ((hFind = FindFirstFile(path, &wfd)) == INVALID_HANDLE_VALUE)
	{
		fprintf(stderr, "Could not scan directory ``%ls''!\n", dirPath);
		return;
	}

	file = path + _tcslen(path) - 1;
	do {
		DWORD attr;

		if (wfd.cFileName[0] == '.')
			continue;

		_tcscpy(file, wfd.cFileName);
		attr = GetFileAttributes(path);

		if (attr & FILE_ATTRIBUTE_DIRECTORY)
			ScanDirectory(path);
		else
		if (attr & (FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_NORMAL))
			IdentifyFile(path);

	} while (FindNextFile(hFind, &wfd) != 0);
}

int main()
{
	ScanDirectory(TEXT("."));
	printf("%12d files scanned in total\n", files_scanned);
	printf("%12d files hard linked\n",  files_linked);
	printf("%12lld bytes saved\n",  bytes_saved);
	return 0;
}
