/**
 * @file orientation_service.h
 *
 * @author Pedro Giló (phvg@ic.ufal.br)
 * @author Thiago Laurentino (tfml@ic.ufal.br)
 * @author Caio Oliveira (cofa@ic.ufal.br)
 *
 * @brief Interface publica do servico de orientacao: face do cubo voltada para
 *        cima, deduzida da gravidade pelo acelerometro.
 * @version 0.1
 * @date 2026-07-05
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef ORIENTATION_SERVICE_H
#define ORIENTATION_SERVICE_H

/**
 * @brief Face do cubo voltada para cima, deduzida da gravidade pelo acelerometro.
 *
 */
enum orientation_face {
	ORIENTATION_UNKNOWN = 0, /**< Inclinado/ambiguo: nenhum eixo domina a gravidade. */
	ORIENTATION_TOP,         /**< Face de cima. */
	ORIENTATION_BOTTOM,      /**< Face de baixo. */
	ORIENTATION_FRONT,       /**< Face da frente. */
	ORIENTATION_BACK,        /**< Face de tras. */
	ORIENTATION_RIGHT,       /**< Face da direita. */
	ORIENTATION_LEFT,        /**< Face da esquerda. */
};

/**
 * @brief Nome legivel da face (para log).
 *
 * @param f Face a converter.
 * @return const char* Texto da face; nunca retorna NULL.
 */
const char *orientation_face_str(enum orientation_face f);

#endif /* ORIENTATION_SERVICE_H */
