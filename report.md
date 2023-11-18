<!-- LTeX: language=pt-BR -->

# PAGINADOR DE MEMÓRIA -- RELATÓRIO

1. Termo de compromisso

    Ao entregar este documento preenchiso, os membros do grupo afirmam que todo o código desenvolvido para este trabalho é de autoria própria.  Exceto pelo material listado no item 3 deste relatório, os membros do grupo afirmam não ter copiado material da Internet nem ter obtido código de terceiros.

2. Membros do grupo e alocação de esforço

    Preencha as linhas abaixo com o nome e o email dos integrantes do grupo.  Substitua marcadores `XX` pela contribuição de cada membro do grupo no desenvolvimento do trabalho (os valores devem somar 100%).

    * João Marcos <jmlg@ufmg.br> 34%
    * Pierre Sousa <pierrevictor@ufmg.br> 33%
    * Mariana Leite <marianaleite@ufmg.br> 33%

3. Referências bibliográficas

    - Slides da disciplina;
    - https://man7.org/linux/man-pages/man3/sysconf.3.html;
    - https://man7.org/linux/man-pages/man3/intptr_t.3type.html;

4. Detalhes de implementação

    1. Descreva e justifique as estruturas de dados utilizadas em sua solução.

        A estrutura geral criada para o paginador contém as informações dos quadros de memória e blocos do disco, de modo a permitir que o paginador gerencie ambos. Além disso, contém uma lista dos processos, um indice circular para o algoritmo da segunda chance, e um `mutex` para assegurar que condições de corrida não irão ocorrer.
        
        ```c
        typedef struct pager {
            // Propriedades referentes aos quadros
            int nframes; // Número máximo de quadros
            int frames_free; // Qnt. de quadros disponíveis
            frame_data_t *frames; // Lista de quadros
            // Propriedades referentes aos blocos
            int nblocks; // Número máximo de blocos
            int blocks_free; // Número de blocos disponíveis
            pid_t *block2pid; // Relação de bloco por processo
            // Propriedade referentes aos processos
            proc_t **pid2proc; // Relação pid para processo
            // Propriedade referente ao alg. da 2ª chance
            int circular_frame_idx; // Indice circular para lista de quadros
            // Propriedade para sincronização
            pthread_mutex_t mutex; // Mutex para sincronização
        } pager_t;
        ```

        Sendo que, para cada quadro da memória, a seguinte estrutura foi estabelecida:

        ```c
        typedef struct frame_data {
            pid_t pid; // Id do processo que detem o quadro
            int page; // Página do processo contida no quadro
            int prot; // Nível de proteção do quadro
            int dirty; // Indica se o quadro foi escrito
        } frame_data_t;
        ```

        A informação do `pid` permite conferir se o quadro está disponível ou não, e a propriedade `page` indica qual página do processo está contida no quadro. Já a propriedade `prot` indica qual o nível de proteção do quadro, e é utilizado principalmente pelo algoritmo de segunda chance. Por fim, a propriedade `dirty` indica se o quadro deve ser transferido para o disco caso o quadro seja liberado para outro processo.

        Para cada processo mapeado, a seguinte estrutura foi estabelecida:

        ```c
        typedef struct proc {
            pid_t pid; // Id do processo
            int npages; // Número de paginas alocadas
            int maxpages;  // Número máximo de páginas alocáveis
            page_data_t *pages; // Lista de páginas do processo
        } proc_t;
        ```

        Por fim, para cada página de um processo, a seguinte estrutura foi estabelecida:

        ```c
        typedef struct page_data {
            int frame; // Quadro em que a página está
            int on_disk; // Indica se a página está no disco
            int block; // Bloco em que a página está
        } page_data_t;
        ```

    2. Descreva o mecanismo utilizado para controle de acesso e modificação às páginas.

        A propriedade `prot`, contida na estrutura de quadros, controla o nível de acesso ao respectivo quadro, podendo ter 3 níveis distintos:
        
        - `PROT_NONE`: Nenhuma operação habilitada;
        - `PROT_READ`: Proteção para apenas leitura;
        - `PROT_READ | PROT_WRITE`: Proteção para leitura e escrita.

        Inicialmente quando um quadro é reservado por um processo, ele inicia com `PROT_NONE`. Porém, na primeira falha de página, quando o quadro é de fato alocado, ele recebe o nível `PROT_READ`, para que seja possível ler seu valor. Se após isso uma falha de página ocorre, indicando que o processo deseja escrever no quadro, o mesmo recebe o nível `PROT_READ | PROT_WRITE`.

        Durante o algoritmo da segunda chance, é verificado se o quadro detém o nível `PROT_NONE` para verificar se o mesmo é elegível a ser liberado, caso contrário, este nível de proteção é atribuito a ele, de modo a dar uma "segunda chance".
